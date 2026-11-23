/* The alert chime needs QNX's audio structures, which this toolchain does not have yet. */
int boost_alert_tone(void) { return 1; }
