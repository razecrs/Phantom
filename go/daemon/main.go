// phantom-daemon — traffic capture server.
//
// Reads PMIT frames from the shared mmap ring buffer written by ssl_tap.c,
// reassembles HTTP/1.1 and HTTP/2 exchanges, feeds the JSON bodies to the
// field scanner, and exposes everything over a local HTTP API + SSE stream
// so the companion hub TUI (and future apps) can subscribe.
//
// API:
//   GET  /events          — SSE stream: one JSON event per line "data: {...}\n\n"
//   GET  /traffic         — JSON array of last 1000 items
//   POST /patch           — body: {"path":"...","value":"...","url":"*"}
//   DELETE /patch/{id}    — remove a patch rule
//   POST /scan            — trigger field scan, returns JSON hits array
//   GET  /status          — {"alive":true,"items":N,"rules":N}

package main

import (
	"bufio"
	"bytes"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
	"unsafe"

	"golang.org/x/sys/unix"
)

// ── ring buffer constants (must match core/ipc/ringbuf.h) ─────────────────
const (
	rbCapacity = 1 << 16 // 64 KB
	rbMask     = rbCapacity - 1
	rbSize     = 8 + 8 + rbCapacity // head(8) + tail(8) + data[]
	rbDataOff  = 16
	rbPath     = "/data/phantom/traffic.rb"
	listenAddr = ":7777"
	maxStore   = 1000
)

// magic "PMIT" little-endian
const frameMagic uint32 = 0x504D4954

// ── PMIT frame header ─────────────────────────────────────────────────────
type frameHdr struct {
	Magic   uint32
	Dir     uint8
	HostLen uint16
	DataLen uint32
}

const frameHdrSize = 4 + 1 + 2 + 4 // 11

// ── traffic item ──────────────────────────────────────────────────────────
type TrafficItem struct {
	ID          uint64            `json:"id"`
	Timestamp   int64             `json:"ts"`
	Dir         string            `json:"dir"`     // "req" | "resp"
	Host        string            `json:"host"`
	Method      string            `json:"method"`
	Path        string            `json:"path"`
	Status      int               `json:"status"`
	ContentType string            `json:"content_type"`
	Headers     map[string]string `json:"headers"`
	Body        string            `json:"body"`      // raw body (may be JSON)
	BodyJSON    interface{}       `json:"body_json"` // parsed if JSON
	Proto       string            `json:"proto"`     // "h1" | "h2"
}

// ── patch rule ────────────────────────────────────────────────────────────
type PatchRule struct {
	ID      uint32 `json:"id"`
	Path    string `json:"path"`
	Value   string `json:"value"`
	Pattern string `json:"url"`
}

// ── SSE client ────────────────────────────────────────────────────────────
type sseClient struct {
	ch chan []byte
}

// ── global state ──────────────────────────────────────────────────────────
var (
	itemSeq   atomic.Uint64
	ruleSeq   uint32

	storeMu sync.RWMutex
	store   []*TrafficItem

	rulesMu sync.RWMutex
	rules   []PatchRule

	ssesMu sync.RWMutex
	sses   []*sseClient
)

// ── ring buffer reader ─────────────────────────────────────────────────────
func openRB(path string) ([]byte, error) {
	fd, err := unix.Open(path, unix.O_RDWR, 0)
	if err != nil {
		return nil, fmt.Errorf("open %s: %w", path, err)
	}
	defer unix.Close(fd)
	m, err := unix.Mmap(fd, 0, rbSize,
		unix.PROT_READ|unix.PROT_WRITE, unix.MAP_SHARED)
	if err != nil {
		return nil, fmt.Errorf("mmap: %w", err)
	}
	return m, nil
}

func rbHead(m []byte) uint64 {
	return atomic.LoadUint64((*uint64)(unsafe.Pointer(&m[0])))
}

func rbTail(m []byte) uint64 {
	return atomic.LoadUint64((*uint64)(unsafe.Pointer(&m[8])))
}

func rbAdvanceTail(m []byte, n uint64) {
	tail := rbTail(m)
	atomic.StoreUint64((*uint64)(unsafe.Pointer(&m[8])), tail+n)
}

func rbByte(m []byte, pos uint64) byte {
	return m[rbDataOff+int(pos&rbMask)]
}

func rbReadN(m []byte, tail uint64, n uint64) []byte {
	out := make([]byte, n)
	for i := uint64(0); i < n; i++ {
		out[i] = m[rbDataOff+int((tail+i)&rbMask)]
	}
	return out
}

// readFrames polls the ring buffer and returns completed frames.
func readFrames(m []byte) []struct {
	dir  uint8
	host string
	data []byte
} {
	var frames []struct {
		dir  uint8
		host string
		data []byte
	}

	for {
		head := rbHead(m)
		tail := rbTail(m)
		avail := head - tail

		if avail < frameHdrSize {
			break
		}

		// read 11-byte header
		hdrBytes := rbReadN(m, tail, frameHdrSize)
		magic := binary.LittleEndian.Uint32(hdrBytes[0:4])
		if magic != frameMagic {
			// desync — advance one byte and retry
			rbAdvanceTail(m, 1)
			continue
		}

		dir := hdrBytes[4]
		hostLen := binary.LittleEndian.Uint16(hdrBytes[5:7])
		dataLen := binary.LittleEndian.Uint32(hdrBytes[7:11])

		total := uint64(frameHdrSize) + uint64(hostLen) + uint64(dataLen)
		if avail < total {
			break // wait for more data
		}

		off := tail + frameHdrSize
		host := string(rbReadN(m, off, uint64(hostLen)))
		off += uint64(hostLen)
		data := rbReadN(m, off, uint64(dataLen))

		rbAdvanceTail(m, total)
		frames = append(frames, struct {
			dir  uint8
			host string
			data []byte
		}{dir, host, data})
	}
	return frames
}

// ── HTTP/1.1 parser ───────────────────────────────────────────────────────
func parseH1(data []byte, host string, dir uint8) *TrafficItem {
	s := string(data)

	item := &TrafficItem{
		Host:    host,
		Proto:   "h1",
		Headers: make(map[string]string),
	}

	if dir == 0 {
		item.Dir = "req"
	} else {
		item.Dir = "resp"
	}

	lines := strings.Split(s, "\r\n")
	if len(lines) == 0 {
		return nil
	}

	// first line
	first := lines[0]
	if dir == 0 {
		// request: "METHOD /path HTTP/1.1"
		parts := strings.SplitN(first, " ", 3)
		if len(parts) >= 2 {
			item.Method = parts[0]
			item.Path = parts[1]
		}
	} else {
		// response: "HTTP/1.1 200 OK"
		parts := strings.SplitN(first, " ", 3)
		if len(parts) >= 2 {
			item.Status, _ = strconv.Atoi(parts[1])
		}
	}

	// headers
	bodyStart := -1
	for i := 1; i < len(lines); i++ {
		if lines[i] == "" {
			bodyStart = i + 1
			break
		}
		kv := strings.SplitN(lines[i], ": ", 2)
		if len(kv) == 2 {
			k := strings.ToLower(kv[0])
			item.Headers[k] = kv[1]
			if k == "content-type" {
				item.ContentType = kv[1]
			}
		}
	}

	// body
	if bodyStart >= 0 && bodyStart <= len(lines) {
		body := strings.Join(lines[bodyStart:], "\r\n")
		body = strings.TrimRight(body, "\x00")
		item.Body = body
		if strings.Contains(item.ContentType, "json") {
			var parsed interface{}
			if json.Unmarshal([]byte(body), &parsed) == nil {
				item.BodyJSON = parsed
			}
		}
	}

	return item
}

// ── HTTP/2 frame parser ───────────────────────────────────────────────────
const (
	h2FrameData         = 0x0
	h2FrameHeaders      = 0x1
	h2FrameSettings     = 0x4
	h2FramePushPromise  = 0x5
	h2FrameGoaway       = 0x7
	h2FrameContinuation = 0x9

	h2Preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
)

// HPACK static table (index 1..61)
var hpackStatic = [62][2]string{
	0:  {"", ""},
	1:  {":authority", ""},
	2:  {":method", "GET"},
	3:  {":method", "POST"},
	4:  {":path", "/"},
	5:  {":path", "/index.html"},
	6:  {":scheme", "http"},
	7:  {":scheme", "https"},
	8:  {":status", "200"},
	9:  {":status", "204"},
	10: {":status", "206"},
	11: {":status", "304"},
	12: {":status", "400"},
	13: {":status", "404"},
	14: {":status", "500"},
	15: {"accept-charset", ""},
	16: {"accept-encoding", "gzip, deflate"},
	17: {"accept-language", ""},
	18: {"accept-ranges", ""},
	19: {"accept", ""},
	20: {"access-control-allow-origin", ""},
	21: {"age", ""},
	22: {"allow", ""},
	23: {"authorization", ""},
	24: {"cache-control", ""},
	25: {"content-disposition", ""},
	26: {"content-encoding", ""},
	27: {"content-language", ""},
	28: {"content-length", ""},
	29: {"content-location", ""},
	30: {"content-range", ""},
	31: {"content-type", ""},
	32: {"cookie", ""},
	33: {"date", ""},
	34: {"etag", ""},
	35: {"expect", ""},
	36: {"expires", ""},
	37: {"from", ""},
	38: {"host", ""},
	39: {"if-match", ""},
	40: {"if-modified-since", ""},
	41: {"if-none-match", ""},
	42: {"if-range", ""},
	43: {"if-unmodified-since", ""},
	44: {"last-modified", ""},
	45: {"link", ""},
	46: {"location", ""},
	47: {"max-forwards", ""},
	48: {"proxy-authenticate", ""},
	49: {"proxy-authorization", ""},
	50: {"range", ""},
	51: {"referer", ""},
	52: {"refresh", ""},
	53: {"retry-after", ""},
	54: {"server", ""},
	55: {"set-cookie", ""},
	56: {"strict-transport-security", ""},
	57: {"transfer-encoding", ""},
	58: {"user-agent", ""},
	59: {"vary", ""},
	60: {"via", ""},
	61: {"www-authenticate", ""},
}

type hpackTable struct {
	dynamic [][2]string
}

func (t *hpackTable) get(idx int) (string, string) {
	if idx <= 0 {
		return "", ""
	}
	if idx <= 61 {
		return hpackStatic[idx][0], hpackStatic[idx][1]
	}
	dynIdx := idx - 62
	if dynIdx < len(t.dynamic) {
		return t.dynamic[dynIdx][0], t.dynamic[dynIdx][1]
	}
	return "", ""
}

func (t *hpackTable) insert(name, value string) {
	entry := [2]string{name, value}
	t.dynamic = append([][2]string{entry}, t.dynamic...)
	if len(t.dynamic) > 64 {
		t.dynamic = t.dynamic[:64]
	}
}

func hpackInt(p []byte, prefixBits uint, pos int) (uint64, int) {
	mask := uint64((1 << prefixBits) - 1)
	val := uint64(p[pos]) & mask
	pos++
	if val < mask {
		return val, pos
	}
	m := 0
	for pos < len(p) {
		b := p[pos]
		pos++
		val += uint64(b&0x7f) << m
		m += 7
		if b&0x80 == 0 {
			break
		}
	}
	return val, pos
}

func hpackString(p []byte, pos int) (string, int) {
	if pos >= len(p) {
		return "", pos
	}
	// huffman flag ignored — copy raw bytes
	slen, pos2 := hpackInt(p, 7, pos)
	end := pos2 + int(slen)
	if end > len(p) {
		end = len(p)
	}
	return string(p[pos2:end]), end
}

type h2Stream struct {
	method      string
	path        string
	authority   string
	status      int
	contentType string
	headers     map[string]string
	body        bytes.Buffer
	endStream   bool
}

type h2Parser struct {
	buf      []byte
	streams  map[uint32]*h2Stream
	hpack    hpackTable
	prefDone bool
}

func newH2Parser() *h2Parser {
	return &h2Parser{streams: make(map[uint32]*h2Stream)}
}

func (p *h2Parser) feed(data []byte, host string, dir uint8) []*TrafficItem {
	p.buf = append(p.buf, data...)

	// strip connection preface (client side)
	if !p.prefDone && len(p.buf) >= len(h2Preface) {
		if bytes.HasPrefix(p.buf, []byte(h2Preface)) {
			p.buf = p.buf[len(h2Preface):]
		}
		p.prefDone = true
	}

	var items []*TrafficItem
	for len(p.buf) >= 9 {
		flen := int(p.buf[0])<<16 | int(p.buf[1])<<8 | int(p.buf[2])
		if len(p.buf) < 9+flen {
			break
		}
		ftype := p.buf[3]
		flags := p.buf[4]
		sid := uint32(p.buf[5]&0x7f)<<24 | uint32(p.buf[6])<<16 |
			uint32(p.buf[7])<<8 | uint32(p.buf[8])
		payload := p.buf[9 : 9+flen]
		p.buf = p.buf[9+flen:]

		if sid == 0 {
			continue // connection-level frame
		}

		s := p.streams[sid]
		if s == nil {
			s = &h2Stream{headers: make(map[string]string)}
			p.streams[sid] = s
		}

		switch ftype {
		case h2FrameHeaders, h2FrameContinuation:
			block := payload
			if ftype == h2FrameHeaders {
				if flags&0x08 != 0 { // PADDED
					pad := int(block[0])
					block = block[1 : len(block)-pad]
				}
				if flags&0x20 != 0 { // PRIORITY
					block = block[5:]
				}
			}
			p.decodeHeaders(s, block)
			if flags&0x01 != 0 { // END_STREAM
				s.endStream = true
				if item := p.completeStream(s, host, dir); item != nil {
					items = append(items, item)
					delete(p.streams, sid)
				}
			}

		case h2FrameData:
			data2 := payload
			if flags&0x08 != 0 { // PADDED
				pad := int(data2[0])
				data2 = data2[1 : len(data2)-pad]
			}
			s.body.Write(data2)
			if flags&0x01 != 0 { // END_STREAM
				s.endStream = true
				if item := p.completeStream(s, host, dir); item != nil {
					items = append(items, item)
					delete(p.streams, sid)
				}
			}
		}
	}
	return items
}

func (p *h2Parser) decodeHeaders(s *h2Stream, block []byte) {
	i := 0
	for i < len(block) {
		b := block[i]
		if b&0x80 != 0 {
			// indexed
			idx, next := hpackInt(block, 7, i)
			i = next
			name, val := p.hpack.get(int(idx))
			p.applyHeader(s, name, val)
		} else if b&0xC0 == 0x40 {
			// literal + indexing
			idx, next := hpackInt(block, 6, i)
			i = next
			var name, val string
			if idx == 0 {
				name, i = hpackString(block, i)
			} else {
				name, _ = p.hpack.get(int(idx))
			}
			val, i = hpackString(block, i)
			p.hpack.insert(name, val)
			p.applyHeader(s, name, val)
		} else {
			// literal without indexing (0x00 or 0x10)
			idx, next := hpackInt(block, 4, i)
			i = next
			var name, val string
			if idx == 0 {
				name, i = hpackString(block, i)
			} else {
				name, _ = p.hpack.get(int(idx))
			}
			val, i = hpackString(block, i)
			p.applyHeader(s, name, val)
		}
	}
}

func (p *h2Parser) applyHeader(s *h2Stream, name, val string) {
	switch name {
	case ":method":
		s.method = val
	case ":path":
		s.path = val
	case ":authority", "host":
		s.authority = val
	case ":status":
		s.status, _ = strconv.Atoi(val)
	case "content-type":
		s.contentType = val
	}
	s.headers[name] = val
}

func (p *h2Parser) completeStream(s *h2Stream, host string, dir uint8) *TrafficItem {
	body := s.body.String()
	item := &TrafficItem{
		Host:        host,
		Proto:       "h2",
		Method:      s.method,
		Path:        s.path,
		Status:      s.status,
		ContentType: s.contentType,
		Headers:     s.headers,
		Body:        body,
	}
	if s.authority != "" {
		item.Host = s.authority
	}
	if dir == 0 {
		item.Dir = "req"
	} else {
		item.Dir = "resp"
	}
	if strings.Contains(s.contentType, "json") && body != "" {
		var parsed interface{}
		if json.Unmarshal([]byte(body), &parsed) == nil {
			item.BodyJSON = parsed
		}
	}
	return item
}

// ── per-host connection state (buffers raw ssl bytes per (host, dir)) ─────
type connState struct {
	h1buf []byte
	h2p   *h2Parser
	isH2  bool
	init  bool
}

var (
	connsMu sync.Mutex
	conns   = make(map[string]*connState) // key = host+":"+dir
)

func connKey(host string, dir uint8) string {
	return host + ":" + strconv.Itoa(int(dir))
}

func processFrame(host string, dir uint8, data []byte) []*TrafficItem {
	connsMu.Lock()
	key := connKey(host, dir)
	cs := conns[key]
	if cs == nil {
		cs = &connState{}
		conns[key] = cs
	}
	connsMu.Unlock()

	// detect protocol on first data
	if !cs.init {
		cs.init = true
		if bytes.HasPrefix(data, []byte(h2Preface)) ||
			bytes.HasPrefix(data, []byte("PRI ")) {
			cs.isH2 = true
			cs.h2p = newH2Parser()
		}
	}

	if cs.isH2 {
		return cs.h2p.feed(data, host, dir)
	}

	// HTTP/1.1 — buffer until we have a complete message
	cs.h1buf = append(cs.h1buf, data...)

	// look for end of HTTP headers (\r\n\r\n)
	sep := []byte("\r\n\r\n")
	idx := bytes.Index(cs.h1buf, sep)
	if idx < 0 {
		return nil
	}

	msg := cs.h1buf
	cs.h1buf = nil // consume — simple single-response-per-connection
	item := parseH1(msg, host, dir)
	if item == nil {
		return nil
	}
	return []*TrafficItem{item}
}

// ── intercept engine (Go-side patch rules) ────────────────────────────────
func globMatch(pat, s string) bool {
	for len(pat) > 0 {
		if pat[0] == '*' {
			pat = pat[1:]
			if len(pat) == 0 {
				return true
			}
			for i := 0; i <= len(s); i++ {
				if globMatch(pat, s[i:]) {
					return true
				}
			}
			return false
		}
		if len(s) == 0 {
			return false
		}
		if pat[0] != '?' && pat[0] != s[0] {
			return false
		}
		pat = pat[1:]
		s = s[1:]
	}
	return len(s) == 0
}

func applyRules(item *TrafficItem) {
	if item.BodyJSON == nil || item.Dir != "resp" {
		return
	}
	rulesMu.RLock()
	rs := append([]PatchRule(nil), rules...)
	rulesMu.RUnlock()

	url := item.Host + item.Path
	changed := false
	for _, r := range rs {
		if !globMatch(r.Pattern, url) {
			continue
		}
		// walk json path and replace
		if patchJSON(item.BodyJSON, strings.Split(r.Path, "."), r.Value) {
			changed = true
		}
	}
	if changed {
		b, _ := json.Marshal(item.BodyJSON)
		item.Body = string(b)
	}
}

func patchJSON(v interface{}, path []string, val string) bool {
	if len(path) == 0 {
		return false
	}
	m, ok := v.(map[string]interface{})
	if !ok {
		return false
	}
	if len(path) == 1 {
		if _, exists := m[path[0]]; !exists {
			return false
		}
		// try to preserve type
		switch m[path[0]].(type) {
		case float64:
			if n, err := strconv.ParseFloat(val, 64); err == nil {
				m[path[0]] = n
				return true
			}
		case bool:
			m[path[0]] = (val == "true" || val == "1")
			return true
		}
		m[path[0]] = val
		return true
	}
	return patchJSON(m[path[0]], path[1:], val)
}

// ── store + broadcast ──────────────────────────────────────────────────────
func storeAndBroadcast(item *TrafficItem) {
	item.ID = itemSeq.Add(1)
	item.Timestamp = time.Now().UnixMilli()

	applyRules(item)

	storeMu.Lock()
	store = append(store, item)
	if len(store) > maxStore {
		store = store[len(store)-maxStore:]
	}
	storeMu.Unlock()

	b, err := json.Marshal(item)
	if err != nil {
		return
	}
	msg := append([]byte("data: "), b...)
	msg = append(msg, '\n', '\n')

	ssesMu.RLock()
	for _, c := range sses {
		select {
		case c.ch <- msg:
		default:
		}
	}
	ssesMu.RUnlock()
}

// ── ring buffer polling loop ───────────────────────────────────────────────
func pollLoop() {
	var m []byte
	for {
		var err error
		m, err = openRB(rbPath)
		if err == nil {
			break
		}
		log.Printf("waiting for ring buffer at %s: %v", rbPath, err)
		time.Sleep(2 * time.Second)
	}
	log.Printf("ring buffer opened: %s", rbPath)

	for {
		frames := readFrames(m)
		for _, f := range frames {
			items := processFrame(f.host, f.dir, f.data)
			for _, item := range items {
				storeAndBroadcast(item)
			}
		}
		if len(frames) == 0 {
			time.Sleep(10 * time.Millisecond)
		}
	}
}

// ── HTTP handlers ──────────────────────────────────────────────────────────
func handleEvents(w http.ResponseWriter, r *http.Request) {
	flusher, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "streaming not supported", 500)
		return
	}
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("X-Accel-Buffering", "no")

	c := &sseClient{ch: make(chan []byte, 64)}
	ssesMu.Lock()
	sses = append(sses, c)
	ssesMu.Unlock()

	defer func() {
		ssesMu.Lock()
		for i, s := range sses {
			if s == c {
				sses = append(sses[:i], sses[i+1:]...)
				break
			}
		}
		ssesMu.Unlock()
	}()

	// send existing store as catch-up
	storeMu.RLock()
	snapshot := append([]*TrafficItem(nil), store...)
	storeMu.RUnlock()
	for _, item := range snapshot {
		b, _ := json.Marshal(item)
		fmt.Fprintf(w, "data: %s\n\n", b)
	}
	flusher.Flush()

	ctx := r.Context()
	for {
		select {
		case msg := <-c.ch:
			w.Write(msg)
			flusher.Flush()
		case <-ctx.Done():
			return
		}
	}
}

func handleTraffic(w http.ResponseWriter, r *http.Request) {
	storeMu.RLock()
	snapshot := append([]*TrafficItem(nil), store...)
	storeMu.RUnlock()
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(snapshot)
}

func handlePatch(w http.ResponseWriter, r *http.Request) {
	if r.Method == http.MethodDelete {
		idStr := strings.TrimPrefix(r.URL.Path, "/patch/")
		id64, err := strconv.ParseUint(idStr, 10, 32)
		if err != nil {
			http.Error(w, "bad id", 400)
			return
		}
		id := uint32(id64)
		rulesMu.Lock()
		for i, ru := range rules {
			if ru.ID == id {
				rules = append(rules[:i], rules[i+1:]...)
				break
			}
		}
		rulesMu.Unlock()
		w.WriteHeader(204)
		return
	}

	var req struct {
		Path    string `json:"path"`
		Value   string `json:"value"`
		Pattern string `json:"url"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad body", 400)
		return
	}
	pat := req.Pattern
	if pat == "" {
		pat = "*"
	}
	rulesMu.Lock()
	ruleSeq++
	rule := PatchRule{ID: ruleSeq, Path: req.Path, Value: req.Value, Pattern: pat}
	rules = append(rules, rule)
	rulesMu.Unlock()

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(rule)
}

func handleScan(w http.ResponseWriter, r *http.Request) {
	// collect all JSON bodies from the store and run a simple key scan
	storeMu.RLock()
	snapshot := append([]*TrafficItem(nil), store...)
	storeMu.RUnlock()

	type Hit struct {
		Path  string      `json:"path"`
		Host  string      `json:"host"`
		Value interface{} `json:"value"`
	}
	hits := map[string]*Hit{}

	var walk func(obj interface{}, prefix string, host string)
	walk = func(obj interface{}, prefix string, host string) {
		switch v := obj.(type) {
		case map[string]interface{}:
			for k, val := range v {
				full := k
				if prefix != "" {
					full = prefix + "." + k
				}
				walk(val, full, host)
			}
		case []interface{}:
			for _, elem := range v {
				walk(elem, prefix, host)
			}
		default:
			if prefix == "" {
				return
			}
			key := host + "|" + prefix
			if _, exists := hits[key]; !exists {
				hits[key] = &Hit{Path: prefix, Host: host, Value: v}
			}
		}
	}

	for _, item := range snapshot {
		if item.BodyJSON != nil && item.Dir == "resp" {
			walk(item.BodyJSON, "", item.Host)
		}
	}

	result := make([]*Hit, 0, len(hits))
	for _, h := range hits {
		result = append(result, h)
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(result)
}

func handleStatus(w http.ResponseWriter, r *http.Request) {
	storeMu.RLock()
	n := len(store)
	storeMu.RUnlock()
	rulesMu.RLock()
	rn := len(rules)
	rulesMu.RUnlock()
	w.Header().Set("Content-Type", "application/json")
	fmt.Fprintf(w, `{"alive":true,"items":%d,"rules":%d}`, n, rn)
}

// ── entry point ────────────────────────────────────────────────────────────
func main() {
	log.SetFlags(log.Ltime | log.Lshortfile)
	log.Println("phantom-daemon starting")

	go pollLoop()

	mux := http.NewServeMux()
	mux.HandleFunc("/events",   handleEvents)
	mux.HandleFunc("/traffic",  handleTraffic)
	mux.HandleFunc("/patch",    handlePatch)
	mux.HandleFunc("/patch/",   handlePatch)
	mux.HandleFunc("/scan",     handleScan)
	mux.HandleFunc("/status",   handleStatus)

	// simple request logger middleware
	handler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		log.Printf("%s %s", r.Method, r.URL.Path)
		mux.ServeHTTP(w, r)
	})

	log.Printf("listening on %s", listenAddr)
	if err := http.ListenAndServe(listenAddr, handler); err != nil {
		log.Fatal(err)
	}
}

// bufio used for SSE buffered scan on hub side
var _ = bufio.NewReader
