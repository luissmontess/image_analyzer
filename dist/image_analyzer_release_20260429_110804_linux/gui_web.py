#!/usr/bin/env python3
"""Web GUI for image_analyzer using only Python standard library.

Run this script, open the shown URL, drop BMP files, and execute C filters.
"""

from __future__ import annotations

import cgi
import datetime as dt
import json
import shutil
import struct
import subprocess
import tempfile
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

REQUIRED_IMAGE_COUNT = 3
REPO_ROOT = Path(__file__).resolve().parent
BINARY_PATH = REPO_ROOT / "image_analyzer"
OUTPUT_DIR = REPO_ROOT / "output"
REPORT_PATH = OUTPUT_DIR / "gui_web_last_run.txt"


HTML_PAGE = """<!doctype html>
<html lang=\"en\">
<head>
  <meta charset=\"utf-8\" />
  <meta name=\"viewport\" content=\"width=device-width,initial-scale=1\" />
  <title>image_analyzer Web GUI</title>
  <style>
    :root {
      --bg-1: #f1f5f3;
      --bg-2: #d9e8f5;
      --ink: #1a2b25;
      --muted: #4c5d55;
      --card: #ffffff;
      --accent: #0e8f66;
      --accent-2: #0a6dc2;
      --danger: #b42318;
      --ring: rgba(14, 143, 102, 0.35);
    }

    * { box-sizing: border-box; }

    body {
      margin: 0;
      font-family: "Segoe UI", "Helvetica Neue", sans-serif;
      color: var(--ink);
      background:
        radial-gradient(circle at 85% 10%, #c9e8d9 0%, transparent 35%),
        radial-gradient(circle at 10% 85%, #cae2f7 0%, transparent 35%),
        linear-gradient(135deg, var(--bg-1), var(--bg-2));
      min-height: 100vh;
    }

    .wrap {
      max-width: 980px;
      margin: 28px auto;
      padding: 0 16px;
    }

    .hero {
      padding: 22px;
      border-radius: 18px;
      background: linear-gradient(120deg, #0e8f66, #0a6dc2);
      color: #fff;
      box-shadow: 0 16px 30px rgba(10, 40, 60, 0.2);
    }

    h1 {
      margin: 0;
      font-size: 2rem;
      letter-spacing: 0.3px;
    }

    .sub {
      margin: 8px 0 0;
      opacity: 0.95;
      font-size: 0.98rem;
    }

    .card {
      margin-top: 16px;
      background: var(--card);
      border-radius: 16px;
      padding: 18px;
      box-shadow: 0 12px 28px rgba(20, 40, 50, 0.12);
    }

    .grid {
      display: grid;
      grid-template-columns: 1fr 200px;
      gap: 10px;
      align-items: end;
      margin-bottom: 14px;
    }

    label {
      display: block;
      font-size: 0.88rem;
      color: var(--muted);
      margin-bottom: 6px;
      font-weight: 600;
    }

    input[type=\"number\"] {
      width: 100%;
      border: 1px solid #c8d4cf;
      border-radius: 10px;
      padding: 10px;
      font-size: 0.98rem;
    }

    .drop {
      border: 2px dashed #8da39a;
      border-radius: 14px;
      padding: 26px;
      text-align: center;
      color: var(--muted);
      background: #f9fcfa;
      transition: all 0.2s ease;
      cursor: pointer;
      margin-bottom: 12px;
    }

    .drop.active {
      border-color: var(--accent);
      background: #eefaf5;
      box-shadow: 0 0 0 5px var(--ring);
    }

    .actions {
      display: flex;
      gap: 10px;
      flex-wrap: wrap;
      margin-bottom: 12px;
    }

    button {
      border: 0;
      border-radius: 10px;
      padding: 10px 14px;
      font-weight: 700;
      cursor: pointer;
      font-size: 0.95rem;
    }

    .run { background: var(--accent); color: #fff; }
    .clear { background: #e7ecea; color: #1a2b25; }
    .open { background: var(--accent-2); color: #fff; }

    .hint {
      font-size: 0.86rem;
      color: var(--muted);
      margin-bottom: 10px;
    }

    ul {
      margin: 0;
      padding-left: 18px;
      max-height: 160px;
      overflow: auto;
    }

    .status {
      white-space: pre-wrap;
      font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
      font-size: 0.86rem;
      background: #0e1620;
      color: #d8e7f2;
      border-radius: 10px;
      padding: 12px;
      min-height: 130px;
    }

    .ok { color: #11a76b; font-weight: 700; }
    .err { color: var(--danger); font-weight: 700; }

    @media (max-width: 720px) {
      .grid { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <div class=\"wrap\">
    <section class=\"hero\">
      <h1>image_analyzer</h1>
      <p class=\"sub\">Drag BMP files here, run C filters from Python, and get outputs + TXT run report.</p>
    </section>

    <section class=\"card\">
      <div class=\"grid\">
        <div>
          <label for=\"threads\">Threads</label>
          <input id=\"threads\" type=\"number\" min=\"1\" max=\"128\" value=\"6\" />
        </div>
        <div class=\"hint\">
          Needs at least 3 valid BMP files (24-bit uncompressed).
        </div>
      </div>

      <input id=\"picker\" type=\"file\" accept=\".bmp,.BMP\" multiple hidden />
      <div id=\"drop\" class=\"drop\">Drop .bmp files here or click to browse</div>

      <div class=\"actions\">
        <button class=\"run\" id=\"run\">Run Filters</button>
        <button class=\"clear\" id=\"clear\">Clear Files</button>
        <button class=\"open\" id=\"open\" type=\"button\">Open Output Folder</button>
      </div>

      <p class=\"hint\" id=\"count\">0 files selected</p>
      <ul id=\"list\"></ul>
    </section>

    <section class=\"card\">
      <div id=\"status\" class=\"status\">Ready.</div>
      <p class=\"hint\">Report path: output/gui_web_last_run.txt</p>
    </section>
  </div>

<script>
  const files = [];
  const fileKeys = new Set();

  const drop = document.getElementById('drop');
  const picker = document.getElementById('picker');
  const list = document.getElementById('list');
  const count = document.getElementById('count');
  const status = document.getElementById('status');
  const runBtn = document.getElementById('run');
  const clearBtn = document.getElementById('clear');
  const openBtn = document.getElementById('open');

  function keyFor(file) {
    return [file.name, file.size, file.lastModified].join('::');
  }

  function renderFiles() {
    list.innerHTML = '';
    files.forEach((f) => {
      const li = document.createElement('li');
      li.textContent = `${f.name} (${Math.round(f.size / 1024)} KB)`;
      list.appendChild(li);
    });
    count.textContent = `${files.length} file(s) selected`;
  }

  function addFiles(fileList) {
    for (const f of fileList) {
      if (!f.name.toLowerCase().endsWith('.bmp')) continue;
      const k = keyFor(f);
      if (fileKeys.has(k)) continue;
      files.push(f);
      fileKeys.add(k);
    }
    renderFiles();
  }

  drop.addEventListener('click', () => picker.click());
  picker.addEventListener('change', (e) => addFiles(e.target.files));

  ['dragenter', 'dragover'].forEach((ev) => {
    drop.addEventListener(ev, (e) => {
      e.preventDefault();
      e.stopPropagation();
      drop.classList.add('active');
    });
  });

  ['dragleave', 'drop'].forEach((ev) => {
    drop.addEventListener(ev, (e) => {
      e.preventDefault();
      e.stopPropagation();
      drop.classList.remove('active');
    });
  });

  drop.addEventListener('drop', (e) => addFiles(e.dataTransfer.files));

  clearBtn.addEventListener('click', () => {
    files.length = 0;
    fileKeys.clear();
    renderFiles();
    status.textContent = 'Cleared.';
  });

  openBtn.addEventListener('click', async () => {
    try {
      const resp = await fetch('/open-output', { method: 'POST' });
      const data = await resp.json();
      status.textContent = data.message;
    } catch (_) {
      status.textContent = 'Could not open output folder from browser mode.';
    }
  });

  runBtn.addEventListener('click', async () => {
    if (files.length === 0) {
      status.innerHTML = '<span class="err">Add BMP files first.</span>';
      return;
    }

    const threads = parseInt(document.getElementById('threads').value || '6', 10);
    const form = new FormData();
    form.append('threads', String(Math.max(1, threads)));
    for (const f of files) form.append('files', f, f.name);

    runBtn.disabled = true;
    status.textContent = 'Running backend...';

    try {
      const resp = await fetch('/run', { method: 'POST', body: form });
      const data = await resp.json();

      if (!resp.ok) {
        status.innerHTML = `<span class="err">${data.error}</span>\n\n${data.details || ''}`;
      } else {
        const out = [
          `Status: OK`,
          `Exit code: ${data.exit_code}`,
          `Used files: ${data.used_files.join(', ')}`,
          `Output dir: ${data.output_dir}`,
          `Report: ${data.report_path}`,
          '',
          'stdout:',
          data.stdout || '<empty>',
          '',
          'stderr:',
          data.stderr || '<empty>'
        ].join('\n');
        status.textContent = out;
      }
    } catch (e) {
      status.innerHTML = `<span class="err">Failed request:</span> ${e}`;
    } finally {
      runBtn.disabled = false;
    }
  });
</script>
</body>
</html>
"""


def is_supported_bmp_24(path: Path) -> bool:
    try:
        with path.open("rb") as f:
            header = f.read(54)
        if len(header) < 54:
            return False
        if header[0:2] != b"BM":
            return False

        bits_per_pixel = struct.unpack_from("<H", header, 28)[0]
        compression = struct.unpack_from("<I", header, 30)[0]
        return bits_per_pixel == 24 and compression == 0
    except OSError:
        return False


def write_report(threads: int, inputs: list[Path], exit_code: int, stdout: str, stderr: str) -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    lines = [
        f"timestamp={dt.datetime.now().isoformat(timespec='seconds')}",
        f"threads={threads}",
        f"exit_code={exit_code}",
        "inputs:",
    ]
    lines.extend([f"- {p}" for p in inputs])
    lines.append("stdout:")
    lines.append(stdout.strip() or "<empty>")
    lines.append("stderr:")
    lines.append(stderr.strip() or "<empty>")
    REPORT_PATH.write_text("\n".join(lines) + "\n", encoding="utf-8")


class Handler(BaseHTTPRequestHandler):
    def _json(self, status: int, payload: dict) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _html(self, status: int, body: str) -> None:
        raw = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):  # noqa: N802
        if self.path in ("/", "/index.html"):
            self._html(200, HTML_PAGE)
            return
        self._html(404, "Not Found")

    def do_POST(self):  # noqa: N802
        if self.path == "/open-output":
            OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
            self._json(200, {"message": f"Output directory: {OUTPUT_DIR}"})
            return

        if self.path != "/run":
            self._json(404, {"error": "Not found"})
            return

        if not BINARY_PATH.exists():
            self._json(500, {"error": f"C binary not found at {BINARY_PATH}. Run 'make' first."})
            return

        form = cgi.FieldStorage(
            fp=self.rfile,
            headers=self.headers,
            environ={
                "REQUEST_METHOD": "POST",
                "CONTENT_TYPE": self.headers.get("Content-Type", ""),
            },
        )

        threads_raw = form.getfirst("threads", "6")
        try:
            threads = max(1, int(threads_raw))
        except ValueError:
            threads = 6

        files_field = form["files"] if "files" in form else []
        if not isinstance(files_field, list):
            files_field = [files_field]

        if len(files_field) == 0:
            self._json(400, {"error": "No files uploaded."})
            return

        tmp_root = Path(tempfile.mkdtemp(prefix="image_analyzer_web_"))
        web_input_dir = tmp_root / "uploaded"
        run_input_dir = tmp_root / "input"
        web_input_dir.mkdir(parents=True, exist_ok=True)
        run_input_dir.mkdir(parents=True, exist_ok=True)

        uploaded_paths: list[Path] = []
        valid_paths: list[Path] = []

        try:
            for i, field in enumerate(files_field):
                if not getattr(field, "filename", None):
                    continue
                src_name = Path(field.filename).name
                target = web_input_dir / f"upload_{i:03d}_{src_name}"
                data = field.file.read()
                target.write_bytes(data)
                uploaded_paths.append(target)

                if is_supported_bmp_24(target):
                    valid_paths.append(target)

            if len(valid_paths) < REQUIRED_IMAGE_COUNT:
                self._json(
                    400,
                    {
                        "error": (
                            f"Need at least {REQUIRED_IMAGE_COUNT} valid 24-bit BMP files; "
                            f"got {len(valid_paths)} valid from {len(uploaded_paths)} uploaded."
                        ),
                        "details": "Unsupported formats (like 32-bit BMP) are skipped.",
                    },
                )
                return

            chosen = valid_paths[:REQUIRED_IMAGE_COUNT]
            copied_names = []
            for i, src in enumerate(chosen, start=1):
                dst = run_input_dir / f"img_{i:02d}_{src.name}"
                shutil.copy2(src, dst)
                copied_names.append(dst.name)

            OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

            cmd = [
                str(BINARY_PATH),
                "--input-dir",
                str(run_input_dir),
                "--output-dir",
                str(OUTPUT_DIR),
                "--threads",
                str(threads),
            ]

            result = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
            write_report(threads, chosen, result.returncode, result.stdout, result.stderr)

            if result.returncode != 0:
                self._json(
                    500,
                    {
                        "error": f"C backend failed with exit code {result.returncode}.",
                        "details": result.stderr.strip() or result.stdout.strip() or "No details.",
                        "exit_code": result.returncode,
                        "report_path": str(REPORT_PATH),
                    },
                )
                return

            self._json(
                200,
                {
                    "exit_code": result.returncode,
                    "used_files": copied_names,
                    "output_dir": str(OUTPUT_DIR / f"{threads}_threads"),
                    "report_path": str(REPORT_PATH),
                    "stdout": result.stdout,
                    "stderr": result.stderr,
                },
            )
        except subprocess.TimeoutExpired:
            write_report(threads, valid_paths[:REQUIRED_IMAGE_COUNT], -1, "", "Timeout")
            self._json(504, {"error": "Backend timeout (10 minutes).", "report_path": str(REPORT_PATH)})
        except Exception as exc:  # noqa: BLE001
            self._json(500, {"error": f"Unexpected error: {exc}"})
        finally:
            shutil.rmtree(tmp_root, ignore_errors=True)


def main() -> int:
    host = "127.0.0.1"
    port = 8765
    server = HTTPServer((host, port), Handler)
    print(f"Web GUI running at http://{host}:{port}")
    print("Press Ctrl+C to stop")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
