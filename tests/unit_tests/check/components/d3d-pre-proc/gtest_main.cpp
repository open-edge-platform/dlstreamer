/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <gtest/gtest.h>

#include <gst/gst.h>

#include <iostream>

GTEST_API_ int main(int argc, char **argv) {
    std::cout << "Running Components::D3DPreProc Test from " << __FILE__ << std::endl;
    try {
        testing::InitGoogleTest(&argc, argv);
        // GstD3D11/GstD3D12 devices, memories and converters are GObjects
        gst_init(&argc, &argv);

        return RUN_ALL_TESTS();
    } catch (const std::exception &e) {
        std::cerr << "Caught std::exception in " << __FILE__ << ": " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Caught unknown exception in " << __FILE__ << std::endl;
        return 1;
    }
}
