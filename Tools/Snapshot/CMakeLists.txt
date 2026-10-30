# Headless visual snapshot of the terrain pipeline (see Docs/building.md). No dependencies.
add_executable(ob_snapshot ObSnapshot.cpp)
target_link_libraries(ob_snapshot PRIVATE OpenBattlefieldCore ob_warnings)

# Also a check: exits non-zero if any cached chunk mesh is stale after renderer-style flushes.
add_test(NAME ob_snapshot_smoke
  COMMAND ob_snapshot --ops 48 --width 320 --height 220 --out ${CMAKE_BINARY_DIR}/snapshot-smoke.png)
add_test(NAME ob_snapshot_greedy_smoke
  COMMAND ob_snapshot --ops 48 --width 320 --height 220 --greedy --edges
          --out ${CMAKE_BINARY_DIR}/snapshot-greedy-smoke.png)
