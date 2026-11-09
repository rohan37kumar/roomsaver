#pragma once

// Show the modal configuration dialog (/c). Loads settings from the registry,
// lets the user edit them (framed picture, idle TV text, beat sensitivity,
// brightness) and
// saves on OK. Returns 0.
int run_config_dialog();
