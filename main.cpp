#include "stdafx.h"

DECLARE_COMPONENT_VERSION(
    "SACD DLNA Server",
    "0.8 alpha 3 U",
    "Native DSD / DVD-Audio UPnP/DLNA Media Server for foobar2000. Uses foo_input_sacd for SACD ISO and foo_input_dvda for DVD-Audio, serving DVD-Audio tracks as cached lossless FLAC."
);

VALIDATE_COMPONENT_FILENAME("foo_sacd_dlna.dll");

FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;
