# Sourced by Pixi on activation (linux-64).

# XMake keys its package cache on the toolchain name, not the compiler version, so give each
# environment its own package directory: a toolchain bump never reuses stale builds. pixi.toml
# is included because activation variables (build flags, pkg-config paths) aren't in the lock.
lock_id=$(cat "$PIXI_PROJECT_ROOT/pixi.toml" "$PIXI_PROJECT_ROOT/pixi.lock" | sha256sum | cut -c1-12)
export XMAKE_PKG_INSTALLDIR="$HOME/.xmake/project_n/$lock_id/packages"

# The env's gdb reads $CONDA_PREFIX/etc/gdbinit on start (the gdb package ships its own
# content there), so append one line sourcing the project's gdbinit, replacing any stale one.
f="$CONDA_PREFIX/etc/gdbinit"
if [ -f "$f" ]; then
    sed -i '/tools\/debug\/gdbinit$/d' "$f"
    echo "source $PIXI_PROJECT_ROOT/tools/debug/gdbinit" >> "$f"
fi
