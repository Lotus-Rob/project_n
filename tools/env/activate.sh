# Sourced by Pixi on activation (linux-64).

# The env's gdb reads $CONDA_PREFIX/etc/gdbinit on start (the gdb package ships its own
# content there), so append one line sourcing the project's gdbinit, replacing any stale one.
f="$CONDA_PREFIX/etc/gdbinit"
if [ -f "$f" ]; then
    sed -i '/tools\/debug\/gdbinit$/d' "$f"
    echo "source $PIXI_PROJECT_ROOT/tools/debug/gdbinit" >> "$f"
fi
