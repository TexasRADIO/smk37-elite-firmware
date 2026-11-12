# SPDX-License-Identifier: GPL-3.0-only
"""Install the Elite app-only loader staging path into the pinned OTA source."""
def loader_header(ota):
    body = ota[32:]
    crc = int.from_bytes(ota[2:4], "little")
    rows = [", ".join(f"0x{byte:02x}" for byte in body[i:i + 16]) for i in range(0, len(body), 16)]
    return (f"#define ELITE_LOADER_CRC 0x{crc:04x}u\n"
            "static const uint8_t elite_loader_body[] = {\n    " + ",\n    ".join(rows) + "\n};\n")


def stage(source):
    anchor = "static int ota_stage(void)"
    begin = "    ota_touched = 1;\n    for (i = 0; i < OTA_AREA_LEN;"
    end = "    /* 6. the host confirms: nothing is committed without it */"
    request = "        if (ota_send_msg(0x30, q, 8))"
    reply = "                for (i = 0; i < len; i++)"
    for marker in (anchor, begin, end, request, reply):
        if source.count(marker) != 1:
            raise ValueError("Unexpected retained OTA staging source")
    source = source.replace(request, "        ota_trace(ELITE_OTA_REQUEST, addr);\n" + request)
    source = source.replace(reply, "                ota_trace(ELITE_OTA_REPLY, addr);\n" + reply)
    source = source.replace(anchor, '#include "board/ota_safe_stage.inc"\n\n' + anchor)
    start, stop = source.index(begin), source.index(end)
    if stop <= start:
        raise ValueError("Unexpected retained OTA staging boundaries")
    official = "".join("    " + line for line in source[start:stop].splitlines(keepends=True))
    return source[:start] + """    if (own && !official) {
        rc = elite_stage_loader(ota_off, len, c, mine);
        if (rc)
            return rc;
    } else {
""" + official + "    }\n" + source[stop:]
