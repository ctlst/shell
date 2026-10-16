#define main gesture_application_main
#include "../ctlst-gestured/ctlst-gestured.c"
#undef main
#include <assert.h>
int main(void)
{
    char runtime[]="/tmp/ctlst-drawer-test-XXXXXX";
    assert(mkdtemp(runtime));
    struct daemon_state state={.runtime_dir=runtime,.drawer_pull_bottom_inset=62};
    state.output.rect=(struct rect){0,0,480,960};
    assert(drawer_pull_geometry_matches(&state,(struct point){240,450},(struct point){240,350}));
    assert(!drawer_pull_geometry_matches(&state,(struct point){240,420},(struct point){240,320}));
    assert(!drawer_pull_geometry_matches(&state,(struct point){240,450},(struct point){160,380}));
    assert(!drawer_pull_geometry_matches(&state,(struct point){240,450},(struct point){240,550}));
    assert(!drawer_pull_geometry_matches(&state,(struct point){240,940},(struct point){240,840}));
    assert(!drawer_pull_geometry_matches(&state,(struct point){20,450},(struct point){20,350}));
    state.output.rect=(struct rect){100,200,960,480};
    assert(drawer_pull_geometry_matches(&state,(struct point){580,430},(struct point){580,330}));
    state.shade_visible=true;
    assert(!drawer_pull_geometry_matches(&state,(struct point){580,430},(struct point){580,330}));
    rmdir(runtime);
    puts("PASS: halfway drawer start, direction/diagonal rejection, protected bottom/sides, landscape offsets, modal gating");
    return 0;
}
