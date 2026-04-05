// phantom-hub — live traffic viewer TUI.
//
// Connects to phantom-daemon's SSE stream and renders an interactive
// terminal UI. Works over adb shell or any terminal.
//
// Usage:
//   ph traffic live [--host localhost:7777]
//   phantom-hub [--host localhost:7777]
//
// Keys:
//   j / ↓      scroll down
//   k / ↑      scroll up
//   Enter       expand/collapse selected item (show body)
//   f           filter by host (type then Enter)
//   p           add a patch rule
//   d           delete selected rule
//   q / Ctrl-C  quit

package main

import (
	"bufio"
	"encoding/json"
	"flag"
	"fmt"
	"net/http"
	"os"
	"strings"
	"sync"
	"time"

	"golang.org/x/term"
)

const (
	reset   = "\033[0m"
	bold    = "\033[1m"
	dim     = "\033[2m"
	red     = "\033[31m"
	green   = "\033[32m"
	yellow  = "\033[33m"
	blue    = "\033[34m"
	magenta = "\033[35m"
	cyan    = "\033[36m"
	white   = "\033[37m"
	bgDark  = "\033[48;5;236m"
	bgSel   = "\033[48;5;238m"

	clearScreen = "\033[2J\033[H"
	clearLine   = "\033[2K"
	moveTo      = "\033[%d;%dH"
	hideCursor  = "\033[?25l"
	showCursor  = "\033[?25h"
	altOn       = "\033[?1049h"
	altOff      = "\033[?1049l"
)

// ── data types (mirrors daemon) ────────────────────────────────────────────
type TrafficItem struct {
	ID          uint64                 `json:"id"`
	Timestamp   int64                  `json:"ts"`
	Dir         string                 `json:"dir"`
	Host        string                 `json:"host"`
	Method      string                 `json:"method"`
	Path        string                 `json:"path"`
	Status      int                    `json:"status"`
	ContentType string                 `json:"content_type"`
	Body        string                 `json:"body"`
	BodyJSON    map[string]interface{} `json:"body_json"`
	Protocol    string                 `json:"proto"`
}

// ── state ──────────────────────────────────────────────────────────────────
var (
	mu       sync.Mutex
	items    []*TrafficItem
	sel      int
	expanded map[uint64]bool
	filter   string

	termW, termH int
)

func init() {
	expanded = make(map[uint64]bool)
}

// ── helpers ────────────────────────────────────────────────────────────────
func methodColor(m string) string {
	switch m {
	case "GET":
		return green
	case "POST":
		return yellow
	case "PUT", "PATCH":
		return cyan
	case "DELETE":
		return red
	}
	return white
}

func statusColor(s int) string {
	switch {
	case s >= 500:
		return red
	case s >= 400:
		return yellow
	case s >= 300:
		return cyan
	case s >= 200:
		return green
	}
	return white
}

func dirIcon(d string) string {
	if d == "req" {
		return magenta + "↑" + reset
	}
	return blue + "↓" + reset
}

func truncate(s string, n int) string {
	if len(s) <= n {
		return s
	}
	return s[:n-1] + "…"
}

func formatTime(ms int64) string {
	t := time.UnixMilli(ms)
	return t.Format("15:04:05")
}

func prettyJSON(body string) string {
	var v interface{}
	if err := json.Unmarshal([]byte(body), &v); err != nil {
		return body
	}
	b, err := json.MarshalIndent(v, "  ", "  ")
	if err != nil {
		return body
	}
	return "  " + string(b)
}

// ── render ─────────────────────────────────────────────────────────────────
func render() {
	mu.Lock()
	defer mu.Unlock()

	termW, termH, _ = term.GetSize(int(os.Stdout.Fd()))
	if termW < 40 {
		termW = 80
	}
	if termH < 10 {
		termH = 24
	}

	var sb strings.Builder

	// move to top
	sb.WriteString(clearScreen)
	sb.WriteString(hideCursor)

	// header bar
	header := fmt.Sprintf(" %sPhantom Traffic%s  %d items  %s[q]quit [↑↓]scroll [Enter]expand [f]filter [p]patch%s",
		bold+cyan, reset, len(items), dim, reset)
	sb.WriteString(bgDark + clearLine + header + reset + "\n")

	// filter bar
	if filter != "" {
		sb.WriteString(dim + " filter: " + cyan + filter + reset + "\n")
	}

	// visible items
	listHeight := termH - 3 // header + filter + status bar
	if filter != "" {
		listHeight--
	}

	visible := filterItems()
	if sel >= len(visible) && len(visible) > 0 {
		sel = len(visible) - 1
	}

	// scroll offset
	offset := 0
	if sel >= listHeight {
		offset = sel - listHeight + 1
	}

	lineCount := 0
	for i := offset; i < len(visible) && lineCount < listHeight; i++ {
		item := visible[i]
		isSelected := i == sel
		isExp := expanded[item.ID]

		bg := ""
		if isSelected {
			bg = bgSel
		}

		// main row
		dirI := dirIcon(item.Dir)
		method := ""
		if item.Method != "" {
			method = methodColor(item.Method) + fmt.Sprintf("%-6s", item.Method) + reset
		}
		statusStr := ""
		if item.Status > 0 {
			statusStr = statusColor(item.Status) + fmt.Sprintf("%3d", item.Status) + reset
		} else {
			statusStr = "   "
		}
		proto := dim + item.Protocol + reset
		host := truncate(item.Host, 30)
		path := truncate(item.Path, termW-70)
		ts := formatTime(item.Timestamp)

		row := fmt.Sprintf(" %s %s%s %s %s%s %s%s %s",
			dirI, bg, ts, proto, method, statusStr,
			cyan+host+reset, dim+path+reset, reset)
		sb.WriteString(row + clearLine + "\n")
		lineCount++

		// expanded body
		if isExp && item.Body != "" {
			lines := strings.Split(prettyJSON(item.Body), "\n")
			maxLines := 20
			if len(lines) < maxLines {
				maxLines = len(lines)
			}
			for _, l := range lines[:maxLines] {
				if lineCount >= listHeight {
					break
				}
				sb.WriteString(dim + "  " + truncate(l, termW-4) + reset + clearLine + "\n")
				lineCount++
			}
		}
	}

	// fill remaining lines
	for lineCount < listHeight {
		sb.WriteString(clearLine + "\n")
		lineCount++
	}

	// status bar
	statusBar := fmt.Sprintf(" %s%d/%d%s  sel=%d",
		bold, len(visible), len(items), reset, sel)
	sb.WriteString(bgDark + clearLine + statusBar + reset)

	fmt.Print(sb.String())
}

func filterItems() []*TrafficItem {
	if filter == "" {
		return items
	}
	f := strings.ToLower(filter)
	var out []*TrafficItem
	for _, item := range items {
		if strings.Contains(strings.ToLower(item.Host), f) ||
			strings.Contains(strings.ToLower(item.Path), f) {
			out = append(out, item)
		}
	}
	return out
}

// ── SSE reader ─────────────────────────────────────────────────────────────
func sseLoop(addr string) {
	url := "http://" + addr + "/events"
	for {
		resp, err := http.Get(url)
		if err != nil {
			fmt.Printf("\r%sconnecting to %s...%s\n", yellow, addr, reset)
			time.Sleep(2 * time.Second)
			continue
		}

		sc := bufio.NewScanner(resp.Body)
		for sc.Scan() {
			line := sc.Text()
			if !strings.HasPrefix(line, "data: ") {
				continue
			}
			data := line[6:]
			var item TrafficItem
			if err := json.Unmarshal([]byte(data), &item); err != nil {
				continue
			}
			mu.Lock()
			items = append(items, &item)
			if len(items) > 2000 {
				items = items[len(items)-2000:]
			}
			mu.Unlock()
			render()
		}
		resp.Body.Close()
		time.Sleep(2 * time.Second)
	}
}

// ── keyboard input ─────────────────────────────────────────────────────────
func inputLoop() {
	oldState, err := term.MakeRaw(int(os.Stdin.Fd()))
	if err != nil {
		return
	}
	defer term.Restore(int(os.Stdin.Fd()), oldState)

	buf := make([]byte, 16)
	for {
		n, err := os.Stdin.Read(buf)
		if err != nil || n == 0 {
			return
		}
		b := buf[:n]

		mu.Lock()
		visible := filterItems()
		mu.Unlock()

		switch {
		case b[0] == 'q' || b[0] == 3: // q or Ctrl-C
			fmt.Print(altOff + showCursor)
			os.Exit(0)

		case b[0] == 'j' || (len(b) == 3 && b[0] == 27 && b[1] == '[' && b[2] == 'B'):
			// j or ↓
			mu.Lock()
			if sel < len(visible)-1 {
				sel++
			}
			mu.Unlock()
			render()

		case b[0] == 'k' || (len(b) == 3 && b[0] == 27 && b[1] == '[' && b[2] == 'A'):
			// k or ↑
			mu.Lock()
			if sel > 0 {
				sel--
			}
			mu.Unlock()
			render()

		case b[0] == '\r' || b[0] == '\n': // Enter
			mu.Lock()
			if sel < len(visible) {
				id := visible[sel].ID
				expanded[id] = !expanded[id]
			}
			mu.Unlock()
			render()

		case b[0] == 'f': // filter
			term.Restore(int(os.Stdin.Fd()), oldState)
			fmt.Print(showCursor + "\n filter host/path: ")
			reader := bufio.NewReader(os.Stdin)
			f, _ := reader.ReadString('\n')
			mu.Lock()
			filter = strings.TrimSpace(f)
			sel = 0
			mu.Unlock()
			term.MakeRaw(int(os.Stdin.Fd()))
			render()

		case b[0] == 'p': // patch
			mu.Lock()
			var target *TrafficItem
			if sel < len(visible) {
				target = visible[sel]
			}
			mu.Unlock()

			term.Restore(int(os.Stdin.Fd()), oldState)
			fmt.Print(showCursor + "\n json_path (e.g. data.coins): ")
			reader := bufio.NewReader(os.Stdin)
			path, _ := reader.ReadString('\n')
			path = strings.TrimSpace(path)

			fmt.Print(" new value: ")
			value, _ := reader.ReadString('\n')
			value = strings.TrimSpace(value)

			if path != "" && value != "" {
				urlPat := "*"
				if target != nil {
					urlPat = "*" + target.Host + "*"
				}
				_ = sendPatch(daemonAddr, path, value, urlPat)
			}
			term.MakeRaw(int(os.Stdin.Fd()))
			render()
		}
	}
}

var daemonAddr string

func sendPatch(addr, path, value, pattern string) error {
	body := fmt.Sprintf(`{"path":%q,"value":%q,"url":%q}`, path, value, pattern)
	resp, err := http.Post("http://"+addr+"/patch", "application/json",
		strings.NewReader(body))
	if err != nil {
		return err
	}
	resp.Body.Close()
	return nil
}

// ── ticker for clock refresh ───────────────────────────────────────────────
func tickRender() {
	for range time.Tick(5 * time.Second) {
		render()
	}
}

// ── main ────────────────────────────────────────────────────────────────────
func main() {
	host := flag.String("host", "localhost:7777", "phantom-daemon address")
	flag.Parse()
	daemonAddr = *host

	fmt.Print(altOn)
	defer fmt.Print(altOff + showCursor)

	go sseLoop(*host)
	go tickRender()

	render()
	inputLoop()
}
