#include "SensorNode.h"

extern "C" void app_main(void) {
    // static: lives for the life of the program, no heap juggling needed
    static SensorNode node;
    node.begin();
}
