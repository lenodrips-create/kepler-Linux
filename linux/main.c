/* kepler-shell: KeplerOS's desktop as a Linux X11 program and window manager. */

#include "host.h"
#include "ui.h"

int main(void)
{
    host_sysinfo_init();
    if (!host_x11_init())
        return 1;
    host_entropy_init();
    shell_main();
    return 0;
}
