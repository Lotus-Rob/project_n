# shaders/: HLSL

## Rules
- One file per stage (`*.vert.hlsl`, `*.frag.hlsl`); shared code lives in `*.hlsli`. `bin/` holds the committed binaries `pixi run project_n shaders` writes: never edit them.
- A comment directly above a helper function or constant that states its contract (what it returns, its inputs' ranges, its promises, what a constant holds) is `///` Doxygen, as in C++ (src/AGENTS.md); a comment that only restates the body goes, and one inside a body stays only to give a why.
- No comment cites a spec, chunk, phase, plan, ruling, task or design doc (`lint` fails on one): say why in your own words.
