"""The real Royale rules (port/linux/game/royale_rules.c): squads of two drawn from the seed, a lethal hit downing
a player with a squadmate standing, a squadmate's held revive (and only theirs, and one let go starting over), a
bleed-out credited to the downer, the gulag asked of every death, squads placed as they go out, the endgame, the
last squad's win and the results' end."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

CASES = ["squads", "revive", "bleed-out", "gulag", "results"]

# a fault in each rule, and the case that must catch it
NEGATIVE_CONTROLS = {
    "revive-not-reset": (("if (entry->reviver != (byte)by)\n\t\t\t\tentry->revive_ticks = 0;", ";"), "revive"),
    "anyone-revives": (("by != mate || ", ""), "revive"),
    "placed-one-low": (("rules->placement[squad] = (byte)(alive + 1);", "rules->placement[squad] = alive;"),
                       "results"),
    "last-of-squad-downed": (("if (!can_revive)", "if (0 && !can_revive)"), "bleed-out"),
    "gulag-not-told": (("if (gulag && gulag(slot, killer_slot) && !in_gulag)",
                        "if (!in_gulag && gulag && gulag(slot, killer_slot))"), "gulag"),
}


def generated(fault=None):
    header = read("port/linux/game/royale_rules.h")
    body = re.sub(r'^#include "(royale_rules|cseries)\.h"$', "", read("port/linux/game/royale_rules.c"), flags=re.M)
    if fault:
        body = mutated(body, *fault)
    return (("royale_rules.inc", header + "\n" + body),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("royale_rules", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("royale_rules", generated(fault)), case)
    assert status == CHECK_FAILED, \
        f"royale_rules with a fault ({control}) passed '{case}': the test cannot see it"
