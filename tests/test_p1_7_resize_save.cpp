#include "rowl_test_harness.hpp"
#include "rowl/c_api.h"
#include <iostream>

void test_p1_7() {
    RowlEngineHandle handle = RowlEngine_Create();
    // Width, height, isOffscreen
    RowlEngine_Init(handle, 1920, 1080, 1);

    RowlEngine_SaveGameSlot(handle, 1);

    std::cout << "Resizing viewport..." << std::endl;
    // Resize to much larger height to trigger ASan failure when reading pixels
    RowlEngine_ResizeViewport(handle, 1920, 4000);
    
    std::cout << "Saving game slot..." << std::endl;
    RowlEngine_SaveGameSlot(handle, 2);

    RowlEngine_Destroy(handle);
    std::cout << "Test passed!" << std::endl;
}

int main() {
    test_p1_7();
    return 0;
}
