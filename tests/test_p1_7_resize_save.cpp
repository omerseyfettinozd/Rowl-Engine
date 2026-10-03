#include "rowl_test_harness.hpp"
#include "rowl/c_api.h"
#include <iostream>

int test_p1_7() {
    RowlEngineHandle handle = RowlEngine_Create();
    // Width, height, isOffscreen
    RowlEngine_Init(handle, 1920, 1080, 1);

    RowlEngine_SaveGameSlot(handle, 1);

    std::cout << "Resizing viewport..." << std::endl;
    // Resize to much larger height to trigger ASan failure when reading pixels
    RowlEngine_ResizeViewport(handle, 1920, 4000);
    
    std::cout << "Saving game slot..." << std::endl;
    RowlEngine_SaveGameSlot(handle, 2);

    // Slot izolasyonu: bu test kalici kullanici veri dizinine (XDG_DATA_HOME
    // -> rowl-engine/saves) yaziyor ve hicbir kaydi silmiyor. Ayni HOME'u
    // paylasan bir sonraki kosuda MS-6 (test_native_c_api.cpp) slot 2'yi
    // "bos" bekliyor ve kirli durumla kirilir; CI'da shaderless adimi ctest'i
    // ikinci kez calistirip tam olarak bu senaryayi uretiyor. Test kendi
    // biraktigi durumu temizlemezse izolasyon hatasi yine yuzeye cikar.
    RowlEngine_DeleteSaveSlot(handle, 1);
    RowlEngine_DeleteSaveSlot(handle, 2);
    // Temizlik sessizce basarisiz olursa bu test yine yesil gorunur ve
    // izolasyon hatasi sonraki kosuda yeniden belirir; bu yuzden dogrula.
    if (RowlEngine_HasSaveSlot(handle, 1) != 0 || RowlEngine_HasSaveSlot(handle, 2) != 0) {
        std::cerr << "P1-7: save slot cleanup failed; this test leaks state into "
                     "the next run (MS-6 expects slots 1 and 2 empty)."
                  << std::endl;
        RowlEngine_Destroy(handle);
        return 1;
    }

    RowlEngine_Destroy(handle);
    std::cout << "Test passed!" << std::endl;
    return 0;
}

int main() {
    return test_p1_7();
}
