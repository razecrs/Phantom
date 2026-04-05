#!/system/bin/sh
# Phantom service — runs after system boot via Magisk/KSU service hook.
# Starts phantom-daemon in background on first invocation.

PHANTOM_DIR=/data/phantom
DAEMON="$PHANTOM_DIR/bin/phantom-daemon"
PID_FILE="$PHANTOM_DIR/daemon.pid"
LOG_FILE="$PHANTOM_DIR/daemon.log"

# wait for data partition to be fully mounted
until [ -d "$PHANTOM_DIR/bin" ]; do sleep 1; done

# kill stale daemon if pid file exists but process is gone
if [ -f "$PID_FILE" ]; then
    OLD_PID=$(cat "$PID_FILE")
    if ! kill -0 "$OLD_PID" 2>/dev/null; then
        rm -f "$PID_FILE"
    fi
fi

# start daemon if not already running
if [ -f "$DAEMON" ] && [ ! -f "$PID_FILE" ]; then
    # ensure ring buffer dir exists
    mkdir -p "$(dirname /data/phantom/traffic.rb)"
    chmod 777 /data/phantom

    # ensure /dev/phantom is writable by injected processes
    mkdir -p /dev/phantom
    chmod 777 /dev/phantom

    nohup "$DAEMON" >> "$LOG_FILE" 2>&1 &
    echo $! > "$PID_FILE"
    log -t Phantom "daemon started (pid=$(cat $PID_FILE))"
fi
