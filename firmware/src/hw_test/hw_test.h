// Hardware test mode: the first thing to flash on a new board. Runs when the app stub is
// active, or always when built with -DDENSITY_HW_TEST (env density_proto_hwtest).
// What it shows is described in README.md.
#pragma once

void hwTestSetup();
void hwTestLoop();
