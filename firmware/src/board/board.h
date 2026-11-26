// Board selection. One board for now: the 125 x 130 mm prototype.
#pragma once
#include "density_proto/proto_hal.h"

using BoardHal = density::proto::ProtoHal;

// The one Hal instance, defined in main.cpp. Board-aware code (the hardware test) uses this
// to reach board extras that density::Hal does not have.
BoardHal& boardHal();
