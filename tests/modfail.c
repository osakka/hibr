/* A module whose init loads a module of its own and then fails: the one
   shape that used to leave a freed record in the shell's module list, since
   m_load's failure path took the *last* entry out rather than its own
   (Gitea #201). It is a fixture of the suite's rather than a real module,
   so the check does not rest on which modules happen to be installed where
   HIBR_MODDIR points. The display module is asked for by interface, the way
   `need` does it, so nothing here carries anyone else's version. */
#include "hibr.h"

extern int m_need(sh *s, const char *nm);

/* Load a module of our own, then fail, which is the shape under test. */
int mf_ini(sh *s)
{
	m_need(s, "display");
	return HIBR_FAIL;
}

HIBR_MODULE_P("modfail", "1.0", "an init that loads another module and fails",
	      0, mf_ini, 0, "modfail");
