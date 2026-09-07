from common import run, RHIZOSECURITYUNIT


def test_security_unit_checks_pass():
    """
    runs the rhizo-security-unit test tool, which covers security fixes
    in code that cannot be driven from the outside through the wire
    protocol - see tests/security_unit.c for the individual checks.
    """
    ret = run([RHIZOSECURITYUNIT])

    assert ret.retval == 0, (
        "rhizo-security-unit reported failing checks:\n"
        + "\n".join(ret.stderr)
    )
