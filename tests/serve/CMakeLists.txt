# SPDX-FileCopyrightText: Copyright 2026 Adobe. All rights reserved. (modifications)
# SPDX-License-Identifier: UPL-1.0 AND Apache-2.0
# Adobe modifications are licensed under Apache-2.0.

# Souffle - A Datalog Compiler
# Copyright (c) 2021 The Souffle Developers. All rights reserved
# Licensed under the Universal Permissive License v 1.0 as shown at:
# - https://opensource.org/licenses/UPL
# - <souffle root>/licenses/SOUFFLE-UPL.txt

# --serve: one translation, several evaluations; each must equal a separate run.
add_test(NAME serve_mode
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/serve_test.py
        $<TARGET_FILE:souffle> ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/serve_mode)

# --serve-deferred-output: selective outputs written later by DUMP.
add_test(NAME serve_deferred_output
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/deferred_test.py
        $<TARGET_FILE:souffle> ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/serve_deferred)

# Repeated requests on one engine (and deferred/memoised variants) must equal fresh runs.
foreach(variant plain deferred)
    add_test(NAME serve_reuse_${variant}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/reuse_test.py
            $<TARGET_FILE:souffle> ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/serve_reuse_${variant} ${variant})
endforeach()
