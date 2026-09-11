from pathlib import Path

path = Path("src/hud/hud-window.cpp")
text = path.read_text(encoding="utf-8")
old = (
    "            if (health.state == HudPageState::NetworkOffline) {\n"
    "                page_connection_recovery_.reset();\n"
    "                page_health_watchdog_.disarm();\n"
    "            } else {\n"
    "                if (page_health_watchdog_.armed()) {\n"
    "                    page_health_watchdog_.heartbeat(now);\n"
    "                } else {\n"
    "                    page_health_watchdog_.arm(now);\n"
    "                }\n"
    "                if (health.state == HudPageState::ConnectionLost) {\n"
    "                    page_connection_recovery_.begin(now);\n"
    "                }\n"
    "            }\n"
)
new = (
    "            if (capture_risk_) {\n"
    "                page_connection_recovery_.reset();\n"
    "                page_health_watchdog_.disarm();\n"
    "            } else if (health.state == HudPageState::NetworkOffline) {\n"
    "                page_connection_recovery_.reset();\n"
    "                page_health_watchdog_.disarm();\n"
    "            } else {\n"
    "                if (page_health_watchdog_.armed()) {\n"
    "                    page_health_watchdog_.heartbeat(now);\n"
    "                } else {\n"
    "                    page_health_watchdog_.arm(now);\n"
    "                }\n"
    "                if (health.state == HudPageState::ConnectionLost) {\n"
    "                    page_connection_recovery_.begin(now);\n"
    "                }\n"
    "            }\n"
)
if text.count(old) != 1:
    raise SystemExit("capture-interlock recovery anchor was not unique")
path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")
