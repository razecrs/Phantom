// phantom-updater — checks for and installs new phantom-module.zip releases.
//
// Usage:
//   phantom-updater check           print latest version and exit (exits 2 if update available)
//   phantom-updater update          download + stage update if newer
//   phantom-updater update --force  stage update regardless of version

package main

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"
)

const (
	phantomDir = "/data/phantom"
	currentVer = phantomDir + "/version"
	updateDir  = phantomDir + "/update"
	moduleZip  = updateDir + "/phantom-module.zip"
	githubAPI  = "https://api.github.com/repos/phantomproject/phantom/releases/latest"
	userAgent  = "phantom-updater/1.0"
	dlTimeout  = 5 * time.Minute
)

type ghRelease struct {
	TagName string    `json:"tag_name"`
	Assets  []ghAsset `json:"assets"`
}

type ghAsset struct {
	Name               string `json:"name"`
	BrowserDownloadURL string `json:"browser_download_url"`
	Size               int64  `json:"size"`
}

func currentVersion() string {
	b, _ := os.ReadFile(currentVer)
	return strings.TrimSpace(string(b))
}

func fetchLatest() (*ghRelease, error) {
	client := &http.Client{Timeout: 30 * time.Second}
	req, _ := http.NewRequest("GET", githubAPI, nil)
	req.Header.Set("User-Agent", userAgent)
	req.Header.Set("Accept", "application/vnd.github.v3+json")
	resp, err := client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("fetch: %w", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode == 404 {
		return &ghRelease{TagName: currentVersion()}, nil
	}
	if resp.StatusCode != 200 {
		return nil, fmt.Errorf("github API %d", resp.StatusCode)
	}
	var r ghRelease
	if err := json.NewDecoder(resp.Body).Decode(&r); err != nil {
		return nil, fmt.Errorf("decode: %w", err)
	}
	return &r, nil
}

func download(url, dest string, expectedSize int64) (string, error) {
	client := &http.Client{Timeout: dlTimeout}
	req, _ := http.NewRequest("GET", url, nil)
	req.Header.Set("User-Agent", userAgent)
	resp, err := client.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()

	os.MkdirAll(filepath.Dir(dest), 0755)
	tmp, err := os.CreateTemp(filepath.Dir(dest), ".dl-*")
	if err != nil {
		return "", err
	}
	h := sha256.New()
	n, err := io.Copy(io.MultiWriter(tmp, h), resp.Body)
	tmp.Close()
	if err != nil {
		os.Remove(tmp.Name())
		return "", err
	}
	if expectedSize > 0 && n != expectedSize {
		os.Remove(tmp.Name())
		return "", fmt.Errorf("size mismatch got=%d want=%d", n, expectedSize)
	}
	sum := hex.EncodeToString(h.Sum(nil))
	if err := os.Rename(tmp.Name(), dest); err != nil {
		os.Remove(tmp.Name())
		return "", err
	}
	return sum, nil
}

func main() {
	cmd := "check"
	force := false
	if len(os.Args) > 1 {
		cmd = os.Args[1]
	}
	if len(os.Args) > 2 && os.Args[2] == "--force" {
		force = true
	}

	r, err := fetchLatest()
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: %v\n", err)
		os.Exit(1)
	}

	cur := currentVersion()

	switch cmd {
	case "check":
		fmt.Printf("current : %s\n", cur)
		fmt.Printf("latest  : %s\n", r.TagName)
		if r.TagName > cur {
			fmt.Println("update available")
			os.Exit(2)
		}
		fmt.Println("up to date")

	case "update":
		if !force && r.TagName <= cur {
			fmt.Printf("already at latest (%s)\n", cur)
			return
		}
		// find zip asset
		var a *ghAsset
		for i := range r.Assets {
			if strings.HasSuffix(r.Assets[i].Name, ".zip") {
				a = &r.Assets[i]
				break
			}
		}
		if a == nil {
			fmt.Fprintln(os.Stderr, "no zip asset in release")
			os.Exit(1)
		}
		fmt.Printf("downloading %s (%d bytes)...\n", a.Name, a.Size)
		sum, err := download(a.BrowserDownloadURL, moduleZip, a.Size)
		if err != nil {
			fmt.Fprintf(os.Stderr, "download failed: %v\n", err)
			os.Exit(1)
		}
		os.WriteFile(currentVer, []byte(r.TagName+"\n"), 0644)
		fmt.Printf("sha256  : %s\n", sum)
		fmt.Printf("staged  : %s\n", moduleZip)
		fmt.Println("flash via Magisk/KSU to apply")

	default:
		fmt.Fprintf(os.Stderr, "usage: phantom-updater [check|update [--force]]\n")
		os.Exit(1)
	}
}
