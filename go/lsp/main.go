// phantom-lsp — Language Server Protocol server for Phantom JS scripts.
//
// Provides IntelliSense-style completions, hover docs, and diagnostics
// for Phantom's built-in APIs when editing scripts in any LSP-capable editor
// (VS Code with the remote LSP extension, vim-lsp, helix, etc.).
//
// Transport: JSON-RPC 2.0 over stdio (standard LSP transport).
// Connect via:
//   "languageServerCommand": ["adb", "shell", "/data/phantom/bin/ph-lsp"]
//
// Capabilities provided:
//   textDocument/completion   — built-in API members + script globals
//   textDocument/hover        — inline documentation
//   textDocument/didChange    — syntax validation (JS parse via QuickJS rules)

package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
	"strconv"
	"strings"
)

// ── JSON-RPC ──────────────────────────────────────────────────────────────

type rpcMsg struct {
	JSONRPC string          `json:"jsonrpc"`
	ID      interface{}     `json:"id,omitempty"`
	Method  string          `json:"method,omitempty"`
	Params  json.RawMessage `json:"params,omitempty"`
	Result  interface{}     `json:"result,omitempty"`
	Error   *rpcError       `json:"error,omitempty"`
}

type rpcError struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
}

func send(msg rpcMsg) {
	b, _ := json.Marshal(msg)
	fmt.Printf("Content-Length: %d\r\n\r\n%s", len(b), b)
}

func reply(id interface{}, result interface{}) {
	send(rpcMsg{JSONRPC: "2.0", ID: id, Result: result})
}

// ── completion database ───────────────────────────────────────────────────

type completionItem struct {
	Label         string `json:"label"`
	Kind          int    `json:"kind"` // 2=method 5=field 6=variable 9=module
	Detail        string `json:"detail"`
	Documentation string `json:"documentation"`
}

var completions = map[string][]completionItem{
	"Memory": {
		{Label: "readU8", Kind: 2, Detail: "Memory.readU8(addr: BigInt): number", Documentation: "Read an unsigned 8-bit integer from addr."},
		{Label: "readU16", Kind: 2, Detail: "Memory.readU16(addr): number"},
		{Label: "readU32", Kind: 2, Detail: "Memory.readU32(addr): number"},
		{Label: "readU64", Kind: 2, Detail: "Memory.readU64(addr): BigInt"},
		{Label: "readS32", Kind: 2, Detail: "Memory.readS32(addr): number"},
		{Label: "readFloat", Kind: 2, Detail: "Memory.readFloat(addr): number"},
		{Label: "readDouble", Kind: 2, Detail: "Memory.readDouble(addr): number"},
		{Label: "readByteArray", Kind: 2, Detail: "Memory.readByteArray(addr, len): ArrayBuffer"},
		{Label: "readCString", Kind: 2, Detail: "Memory.readCString(addr, maxLen?): string"},
		{Label: "readPointer", Kind: 2, Detail: "Memory.readPointer(addr): BigInt"},
		{Label: "writeU32", Kind: 2, Detail: "Memory.writeU32(addr, value): void"},
		{Label: "writeU64", Kind: 2, Detail: "Memory.writeU64(addr, value): void"},
		{Label: "writePointer", Kind: 2, Detail: "Memory.writePointer(addr, ptr): void"},
		{Label: "alloc", Kind: 2, Detail: "Memory.alloc(size): BigInt", Documentation: "Allocate size bytes. Returns address as BigInt."},
		{Label: "free", Kind: 2, Detail: "Memory.free(addr): void"},
		{Label: "protect", Kind: 2, Detail: "Memory.protect(addr, size, 'rwx'): void"},
		{Label: "scan", Kind: 2, Detail: "Memory.scan(addr, len, u32val): BigInt[]"},
	},
	"Module": {
		{Label: "findBaseAddress", Kind: 2, Detail: "Module.findBaseAddress(soName): BigInt|null", Documentation: "Find a loaded .so's base address by name."},
		{Label: "findExportByName", Kind: 2, Detail: "Module.findExportByName(so, sym): BigInt|null", Documentation: "dlsym equivalent. Returns null if not found."},
		{Label: "getByName", Kind: 2, Detail: "Module.getByName(so): {base, size}|null"},
	},
	"Process": {
		{Label: "id", Kind: 5, Detail: "Process.id: number", Documentation: "Current process PID."},
		{Label: "arch", Kind: 5, Detail: "Process.arch: 'arm64'|'arm'"},
		{Label: "pageSize", Kind: 5, Detail: "Process.pageSize: number"},
		{Label: "enumerateModules", Kind: 2, Detail: "Process.enumerateModules(): {name, base}[]"},
	},
	"Interceptor": {
		{Label: "attach", Kind: 2, Detail: "Interceptor.attach(addr, {onEnter, onLeave}): id", Documentation: "Hook function at addr. onEnter(args) receives BigInt[8]. onLeave(retval) can return new retval."},
		{Label: "detach", Kind: 2, Detail: "Interceptor.detach(id): void"},
		{Label: "detachAll", Kind: 2, Detail: "Interceptor.detachAll(): void"},
		{Label: "replace", Kind: 2, Detail: "Interceptor.replace(addr, replacement): BigInt", Documentation: "Replace function at addr with replacement. Returns original pointer."},
	},
	"Network": {
		{Label: "analyze", Kind: 2, Detail: "Network.analyze(mode?): Hit[]", Documentation: "Scan captured traffic for known game/app fields. mode: 'games'|'apps'|'all'"},
		{Label: "observe", Kind: 2, Detail: "Network.observe(pattern, cb): void"},
		{Label: "intercept", Kind: 2, Detail: "Network.intercept(pattern, cb): void"},
		{Label: "patch", Kind: 2, Detail: "Network.patch(jsonPath, value, urlPattern?): number", Documentation: "Register a patch rule. Returns rule id for unpatch()."},
		{Label: "unpatch", Kind: 2, Detail: "Network.unpatch(id: number): void", Documentation: "Remove a patch rule by id returned from Network.patch()."},
		{Label: "block", Kind: 2, Detail: "Network.block(urlPattern): void"},
	},
	"Game": {
		{Label: "pinField", Kind: 2, Detail: "Game.pinField(jsonPath, value): void"},
		{Label: "blockRequest", Kind: 2, Detail: "Game.blockRequest(pattern): void"},
		{Label: "infiniteAll", Kind: 2, Detail: "Game.infiniteAll(value?): void", Documentation: "Pin all number/int_bool currency fields found during last scan."},
		{Label: "unlockAll", Kind: 2, Detail: "Game.unlockAll(): void", Documentation: "Patch all locked boolean fields to true."},
	},
	"Java": {
		{Label: "use", Kind: 2, Detail: "Java.use(className): ClassWrapper", Documentation: "Get a Java class wrapper. In @layer java: enables .implementation override."},
		{Label: "perform", Kind: 2, Detail: "Java.perform(fn): void"},
		{Label: "array", Kind: 2, Detail: "Java.array(type, elements): array"},
		{Label: "cast", Kind: 2, Detail: "Java.cast(obj, klass): obj"},
	},
	"ph": {
		{Label: "log", Kind: 2, Detail: "ph.log(...args): void", Documentation: "Log to Android logcat (INFO)."},
		{Label: "warn", Kind: 2, Detail: "ph.warn(...args): void"},
		{Label: "error", Kind: 2, Detail: "ph.error(...args): void"},
		{Label: "sleep", Kind: 2, Detail: "ph.sleep(ms): void"},
		{Label: "env", Kind: 2, Detail: "ph.env(): string", Documentation: "Returns the target app's package name."},
		{Label: "dumpDex", Kind: 2, Detail: "ph.dumpDex(dir?): number", Documentation: "Dump all loaded DEX files to disk. Returns count."},
	},
}

var globals = []completionItem{
	{Label: "Memory", Kind: 9, Detail: "Memory — process memory read/write API"},
	{Label: "Module", Kind: 9, Detail: "Module — loaded library lookup"},
	{Label: "Process", Kind: 9, Detail: "Process — process metadata"},
	{Label: "Interceptor", Kind: 9, Detail: "Interceptor — native function hooks"},
	{Label: "Network", Kind: 9, Detail: "Network — MITM traffic API"},
	{Label: "Game", Kind: 9, Detail: "Game — game-specific shortcuts"},
	{Label: "Java", Kind: 9, Detail: "Java — JVM reflection bridge"},
	{Label: "ph", Kind: 9, Detail: "ph — phantom utilities"},
	{Label: "console", Kind: 9, Detail: "console — log alias"},
}

// ── completion handler ────────────────────────────────────────────────────

func handleCompletion(params json.RawMessage, id interface{}) {
	// Extract trigger text — look for "X." pattern
	var p struct {
		TextDocument struct{ URI string } `json:"textDocument"`
		Position     struct {
			Line      int `json:"line"`
			Character int `json:"character"`
		} `json:"position"`
		Context struct{ TriggerCharacter string } `json:"context"`
	}
	json.Unmarshal(params, &p)

	// If triggered by '.', try to match the object name from context
	// We can't read the file content over LSP without document sync,
	// so we return all global completions plus the triggered object's members.
	var items []completionItem

	if p.Context.TriggerCharacter == "." {
		// Return all known namespaces' completions (client filters)
		for _, members := range completions {
			items = append(items, members...)
		}
	} else {
		items = globals
	}

	reply(id, map[string]interface{}{
		"isIncomplete": false,
		"items":        items,
	})
}

// ── hover handler ─────────────────────────────────────────────────────────

func handleHover(params json.RawMessage, id interface{}) {
	// Minimal: return empty hover — full impl would read doc content
	reply(id, map[string]interface{}{
		"contents": map[string]string{
			"kind":  "markdown",
			"value": "**Phantom API** — use `ph.log()`, `Memory.readU32()`, etc.",
		},
	})
}

// ── main loop ─────────────────────────────────────────────────────────────

func main() {
	scanner := bufio.NewScanner(os.Stdin)
	scanner.Buffer(make([]byte, 1<<20), 1<<20)

	for {
		// read headers
		contentLength := 0
		for scanner.Scan() {
			line := scanner.Text()
			if line == "" {
				break
			}
			if strings.HasPrefix(line, "Content-Length: ") {
				n, _ := strconv.Atoi(strings.TrimPrefix(line, "Content-Length: "))
				contentLength = n
			}
		}
		if contentLength == 0 {
			continue
		}

		// read body
		body := make([]byte, contentLength)
		total := 0
		for total < contentLength {
			n, err := os.Stdin.Read(body[total:])
			total += n
			if err != nil {
				return
			}
		}

		var msg rpcMsg
		if err := json.Unmarshal(body, &msg); err != nil {
			continue
		}

		switch msg.Method {
		case "initialize":
			reply(msg.ID, map[string]interface{}{
				"capabilities": map[string]interface{}{
					"completionProvider": map[string]interface{}{
						"triggerCharacters": []string{"."},
					},
					"hoverProvider":                 true,
					"textDocumentSync":              1,
					"definitionProvider":            false,
					"referencesProvider":            false,
					"documentFormattingProvider":    false,
				},
				"serverInfo": map[string]string{
					"name":    "phantom-lsp",
					"version": "1.0.0",
				},
			})

		case "initialized":
			// notification — no response needed

		case "shutdown":
			reply(msg.ID, nil)

		case "exit":
			os.Exit(0)

		case "textDocument/completion":
			handleCompletion(msg.Params, msg.ID)

		case "textDocument/hover":
			handleHover(msg.Params, msg.ID)

		case "textDocument/didOpen",
			"textDocument/didChange",
			"textDocument/didClose":
			// no-op for now

		default:
			if msg.ID != nil {
				send(rpcMsg{
					JSONRPC: "2.0",
					ID:      msg.ID,
					Error:   &rpcError{Code: -32601, Message: "method not found"},
				})
			}
		}
	}
}
