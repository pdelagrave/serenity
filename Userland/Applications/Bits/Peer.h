/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/IPv4Address.h>
#include <AK/Types.h>

namespace Bits {

class Peer {
private:
    u8 id[20];
    IPv4Address address;
    u16 port;
};

}
