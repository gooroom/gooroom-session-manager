%.desktop: %.desktop.in
	$(AM_V_GEN) sed -e "s|\@pkglibexecdir\@|$(pkglibexecdir)|" $< > $@

autostartdir = $(sysconfdir)/xdg/autostart
autostart_DATA = gooroom-session-manager.desktop

@INTLTOOL_POLICY_RULE@

polkitdir = $(datadir)/polkit-1/actions
polkit_in_files = kr.gooroom.SessionManager.policy.in
polkit_DATA = $(polkit_in_files:.policy.in=.policy)

CLEANFILES = \
	$(autostart_DATA) \
	$(polkit_DATA)
