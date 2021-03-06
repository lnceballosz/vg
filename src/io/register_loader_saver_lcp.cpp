// SPDX-FileCopyrightText: 2014 Erik Garrison
//
// SPDX-License-Identifier: MIT

/**
 * \file register_loader_saver_lcp.cpp
 * Defines IO for a GCSA LCPArray from stream files.
 */

#include <vg/io/registry.hpp>

#include <gcsa/gcsa.h>
#include <gcsa/algorithms.h>

namespace vg {

namespace io {

using namespace std;
using namespace vg::io;

void register_loader_saver_lcp() {
    Registry::register_bare_loader_saver<gcsa::LCPArray>("LCP", [](istream& input) -> void* {
        // Allocate an LCPArray
        gcsa::LCPArray* index = new gcsa::LCPArray();
        
        // Load it
        index->load(input);
        
        // Return it so the caller owns it.
        return (void*) index;
    }, [](const void* index_void, ostream& output) {
        // Cast to LCP and serialize to the stream.
        ((const gcsa::LCPArray*) index_void)->serialize(output);
    });

}

}

}

