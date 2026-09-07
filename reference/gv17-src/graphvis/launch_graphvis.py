"""Small GraphVis launcher with startup and unexpected-exit diagnostics.

Environment management stays in update.bat.  This launcher starts the
Qt-independent session journal before importing PySide6, so even startup
failures have a useful report under Documents/GraphVis/Logs/Errors.
"""
from __future__ import annotations

from datetime import datetime
import traceback

from graphvis.core.paths import ERROR_LOG_DIR, ensure_layout
from graphvis.core.session_diagnostics import end_session, record_activity, record_exception, start_session

ensure_layout()
LAUNCH_LOG = ERROR_LOG_DIR / "launcher.log"


def _record(text: str) -> None:
    with LAUNCH_LOG.open("a", encoding="utf-8") as fh:
        fh.write(f"\n[{datetime.now().isoformat(timespec='seconds')}]\n{text}\n")


def main() -> int:
    start_session()
    code = 0
    try:
        record_activity("Importing GraphVis application")
        from graphvis.main_app import main as app_main
        record_activity("Starting Qt application loop")
        app_main()
        return 0
    except SystemExit as exc:
        code = exc.code if isinstance(exc.code, int) else 0
        code = int(code or 0)
        if code:
            msg = f"GraphVis exited with SystemExit({code})."
            _record(msg)
            record_exception(msg, traceback_text="No Python traceback was attached to this non-zero SystemExit.", fatal=True)
        return code
    except BaseException as exc:
        code = 1
        tb = traceback.format_exc()
        _record(tb)
        record_exception("GraphVis launcher/application exception", exc=exc, traceback_text=tb, fatal=True)
        print(tb)
        print(f"Startup/error report saved under: {ERROR_LOG_DIR}")
        return 1
    finally:
        end_session(code, "Normal application exit" if code == 0 else f"Application exited with code {code}")


if __name__ == "__main__":
    raise SystemExit(main())
