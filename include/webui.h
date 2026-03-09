#ifndef WEBUI_H
#define WEBUI_H

/// Initialize the async web server (port 80) with REST API + HTML UI
void webui_init();

/// Call periodically to handle any needed background tasks
void webui_update();

#endif // WEBUI_H
