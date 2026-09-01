/*
 * The one translation unit that instantiates metal-cpp.
 *
 * metal-cpp is header-only inline bindings over objc_msgSend, and those
 * bindings reference per-class/per-selector tables that have to be *defined*
 * exactly once in the whole link. Defining these three macros before the
 * includes is what emits them; every other file in this backend includes the
 * same headers without the macros and so only references the tables.
 *
 * Consequences worth knowing before touching this file:
 *  - Do not add anything else here. A second definition of the tables (from
 *    defining the macros in a second TU) is a duplicate-symbol link error;
 *    zero definitions is an undefined-symbol link error naming selectors
 *    rather than anything recognisable.
 *  - MetalFX is not vendored (see vendor/metal-cpp/AURA_PROVENANCE.md), so
 *    MTLFX_PRIVATE_IMPLEMENTATION is deliberately absent.
 */

#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
