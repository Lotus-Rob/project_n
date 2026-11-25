# Review: gap1-logging-and-comment-rule-conformance (part 2)

### GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-11: Duplicated and mis-levelled INFO lines on user actions

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** logging-levels · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/app/app.cpp:652`

A click on an entity logs DEBUG 'clicked on entity N' (app.cpp:652) and then `Host::Select` logs INFO 'selected entity N' for the same click, so one action is logged twice at two levels. `Host::Select` logs INFO for every selection or deselection click. 'tick N: the debug burst has no foxes to hit in this scene' (host.cpp:1429) is INFO for a debug key that did nothing; the matching ignored-action cases are DEBUG. 'month step cut short: the game is closing' (npcs.cpp:375) and 'Month step: ...' (npcs.cpp:390, capitalised unlike every other message) are inconsistent in style. `Game::Step` logs INFO 'an expected stall (a month, a save, a load, a replay or the wait for a scene) took N ms' (game.cpp:44) naming five possible causes rather than the actual one.

**Impact:** Minor noise, inconsistency, and one log line that names no cause.

**Recommendation:** Keep the outcome (Select) and drop the click echo, or demote Select to DEBUG. Make the no-fox burst DEBUG. Lower-case 'Month step'. Pass the actual reason from `RunBurst` to the stall line.

**Verification:** Verified: app.cpp:652 logs DEBUG 'clicked on entity N' and Host::Select (host.cpp:1539) logs INFO 'selected ...' for every click. host.cpp:1429 no-fox burst is INFO. npcs.cpp:375 and :390 differ in capitalisation ('Month step:' is DEBUG). The stall line is in app/game.cpp:45, not game.cpp:44, and names five possible causes. Style nits only.

### GAP1-LOGGING-AND-COMMENT-RULE-CONFORMANCE-12: Plain-comment rule: what-style comments on private declarations written with // instead of Doxygen, and a few placement/describing comments

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** comment-rule · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/app/app.hpp:223`

Of about 460 plain in-body and member comments sampled across src/game, src/app and src/gfx, nearly all explain why. The offenders cluster in two patterns. (1) Private member and method declarations documented with `//` describing what they do, which the rule says belongs in `///`: app.hpp:223 'Hands the camera's controls ... to it', app.hpp:234 'Walks to the cursor's tile and hides it', app.hpp:254 'Moves `--camera-path` on to `nowNs`: starts it, logs each segment's start...', labels.hpp:195/206/220/229/244 ('A box a stack covers', 'Adds the stacks...', 'Joins the label...'), host.hpp:458 'Drops what each journey kept...', seals.hpp:118 'Makes `buffer` and `transfer` hold...'. (2) Plain what-comments inside code: game/scenes/arena.cpp:44 'Standing between the dead tree and the crystal.' and :48 'Below the fight, between the tablet and the crane.' (placement notes restating the coordinates, though neighbours give the why, 'nothing stands on it'), app.cpp:281 'An exception that escapes is logged and stops the game.' (restates the next lines), gfx/camera.cpp:25 'Clip space spans 2 across the target.' (borderline fact). No design-process citations were found in the sample.

**Impact:** Low. It does not break the lint's design-reference check, but it departs from the 'plain comment explains only why' rule and Doxygen won't pick up the declaration docs.

**Recommendation:** Convert the private-declaration descriptions to `///` (or delete those that restate the name). Fold the arena placement notes into the why they already hold. Consider a lint heuristic for plain comments beginning with a third-person verb directly above a declaration.

**Verification:** app.hpp:223 has a '//' what-description on a private declaration, and arena.cpp:44 and :48 are placement-only comments ('Standing between...', 'Below the fight...'). app.cpp:281 'An exception that escapes is logged and stops the game.' restates the try block. The rule only requires plain comments to explain why, and Doxygen is for the API, so private '//' notes are borderline. Minor.
