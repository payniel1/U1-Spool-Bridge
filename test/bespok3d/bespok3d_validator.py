"""Bespok3d rfid-ntag 0.1.14's filament_detect/set validator, transcribed.

Source: Bespok3d/u1-enhanced-rfid @ main, plugin version 0.1.14
        rfid-ntag/files/klipper/klippy/extras/rfid-support/rfid_ntag.py

Only the parts that decide accept-or-refuse are reproduced. The point is to
put THIS firmware's real payload through THEIR rules, so a field they do not
know is caught here rather than on a drybox at the far end of the house.
"""

DESCRIBES = "Bespok3d rfid-ntag 0.1.14 (app 0.7.6-beta, daemon 0.14.0)"

STRING_FIELDS = ("VENDOR", "MAIN_TYPE", "SUB_TYPE")
INT_FIELDS    = ("HOTEND_MIN_TEMP", "HOTEND_MAX_TEMP", "BED_TEMP")
OPT_RGB       = ("RGB_2", "RGB_3", "RGB_4", "RGB_5")


class Refused(Exception):
    pass


def _apply_named_fields(info, params):
    for k in STRING_FIELDS:
        if k in params:
            info[k] = str(params.pop(k))
    for k in INT_FIELDS:
        if k in params:
            info[k] = int(params.pop(k))


def _apply_colors(info, params):
    if "ALPHA" in params:
        info["ALPHA"] = int(params.pop("ALPHA")) & 0xFF
    if "RGB_1" in params:
        info["RGB_1"] = int(params.pop("RGB_1")) & 0xFFFFFF
    for k in OPT_RGB:
        if k in params:
            info[k] = int(params.pop(k)) & 0xFFFFFF


def _apply_misc_fields(info, params):
    if "MULTI_MODE" in params:
        info["MULTI_MODE"] = int(params.pop("MULTI_MODE")) & 0xFF
    if "CARD_UID" in params:
        info["CARD_UID"] = [int(b) for b in params.pop("CARD_UID")]
    if "SKU" in params:
        info["SKU"] = params.pop("SKU")


def _build_filament_info(params):
    info = {}
    _apply_named_fields(info, params)
    _apply_colors(info, params)
    _apply_misc_fields(info, params)
    return info


def handle(body, channel_count=4):
    """-> the JSON the endpoint answers with. Mirrors _handle_filament_detect_set,
    including that it catches its own errors and still returns HTTP 200."""
    try:
        channel = body.get("channel")
        if channel is None:
            raise Refused("channel must be specified!")
        if not isinstance(channel, int):
            raise Refused("channel must be an integer")
        if channel < 0 or channel >= channel_count:
            raise Refused("channel[%d] is out of range[0, %d]" % (channel, channel_count - 1))

        params = dict(body.get("info", {}))
        has_params = len(params) > 0
        info = _build_filament_info(params)
        if params:                       # whatever did not get popped
            raise Refused("unsupported fields: %s" % ", ".join(sorted(params.keys())))
        info["OFFICIAL"] = has_params
        return {"state": "success"}
    except Exception as err:
        return {"state": "error", "message": str(err)}
