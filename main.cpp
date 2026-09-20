#include "stdafx.h"

DECLARE_COMPONENT_VERSION(
    "SACD DLNA Server",
    "0.8 alpha 3 J",
    "Native DSD UPnP/DLNA Media Server for foobar2000. Requires the Super Audio CD Decoder (foo_input_sacd). Provides Artist/Album/Track browsing, album art and on-demand DSF generation for SACD ISO."
);

VALIDATE_COMPONENT_FILENAME("foo_sacd_dlna.dll");

FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;
