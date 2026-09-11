/**
 * test_aspect_guardian.cpp — AspectGuardian letterbox/pillarbox math.
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"

void test_aspect_guardian() {
    TEST_SECTION("AspectGuardian Subsystem");

    // 16:9 Virtual canvas (1920x1080) on 16:9 physical display (1920x1080)
    auto m1 = Rowl::Render::AspectGuardian::calculateViewport(1920, 1080, 1920, 1080);
    if (m1.width != 1920 || m1.height != 1080 || m1.isPillarbox) {
        std::cerr << "Aspect mismatch 16:9" << std::endl;
        exit(1);
    }
    TEST_PASS("1:1 Perfect Aspect Match (1920x1080)");

    // 16:9 Virtual canvas on 21:9 Ultra-Wide display (2560x1080) -> Pillarbox (bars on sides)
    auto m2 = Rowl::Render::AspectGuardian::calculateViewport(2560, 1080, 1920, 1080);
    if (!m2.isPillarbox || m2.width != 1920 || m2.x != 320) {
        std::cerr << "Aspect mismatch 21:9" << std::endl;
        exit(1);
    }
    TEST_PASS("21:9 Ultra-Wide Pillarbox Calculation (2560x1080)");

    // 16:9 Virtual canvas on 4:3 Box display (1024x768) -> Letterbox (bars on top/bottom)
    auto m3 = Rowl::Render::AspectGuardian::calculateViewport(1024, 768, 1920, 1080);
    if (m3.isPillarbox || m3.width != 1024 || m3.y <= 0) {
        std::cerr << "Aspect mismatch 4:3" << std::endl;
        exit(1);
    }
    TEST_PASS("4:3 Letterbox Calculation (1024x768)");

    // Coordinate conversion
    float physX = 0, physY = 0;
    Rowl::Render::AspectGuardian::virtualToPhysical(960.0f, 540.0f, m1, physX, physY);
    if (std::abs(physX - 960.0f) > 0.01f || std::abs(physY - 540.0f) > 0.01f) {
        std::cerr << "Coordinate conversion mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Virtual to Physical Coordinate Projection");

    const auto portrait = Rowl::Render::AspectGuardian::calculateViewport(1080, 1920, 1920, 1080);
    if (Rowl::Render::AspectGuardian::containsPhysicalPoint(540.0f, 100.0f, portrait) ||
        !Rowl::Render::AspectGuardian::containsPhysicalPoint(540.0f, 960.0f, portrait)) {
        std::cerr << "Portrait letterbox hit-test bounds mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Portrait Letterbox Input Bounds");
}
