#include "stdafx.h"

DECLARE_COMPONENT_VERSION(
    "SACD DLNA Server",
    "1.0.0",
    "Native DSD / DVD-Audio UPnP/DLNA Media Server for foobar2000. Uses foo_input_sacd for SACD ISO and foo_input_dvda for DVD-Audio, serving DVD-Audio tracks as cached lossless FLAC via the official libFLAC 1.5.x encoder."
);

VALIDATE_COMPONENT_FILENAME("foo_sacd_dlna.dll");

FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;
