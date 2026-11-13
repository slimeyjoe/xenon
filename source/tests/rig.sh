# The test rigs' machine settings (rig.conf.example). Sourced, not run, after the caller set
# RIG_TESTS to the tests/ directory: sets every KEY of $RIG_TESTS/rig.conf (or $RIG_CONF) that
# the environment does not already set, and exports it.
#
#   rig_need VAR "what it is"     stop with a hint when VAR is empty
_rig_conf=${RIG_CONF:-$RIG_TESTS/rig.conf}
[ -f "$_rig_conf" ] || _rig_conf=
if [ -n "$_rig_conf" ]; then
	while IFS= read -r _rig_line || [ -n "$_rig_line" ]; do
		case $_rig_line in
		'' | '#'*) continue ;;
		esac
		_rig_key=${_rig_line%%=*}
		_rig_val=${_rig_line#*=}
		case $_rig_key in
		*[!A-Za-z0-9_]* | '') continue ;;
		esac
		case $_rig_val in
		'~/'*) _rig_val=$HOME/${_rig_val#'~/'} ;;
		esac
		eval "_rig_set=\${$_rig_key+x}"
		[ -n "$_rig_set" ] && continue
		eval "$_rig_key=\$_rig_val"
		export "$_rig_key"
	done < "$_rig_conf"
fi

rig_need()
{
	eval "_rig_v=\${$1:-}"
	[ -n "$_rig_v" ] && return 0
	echo "$(basename "$0"): set $1 ($2) in source/tests/rig.conf or the environment" \
	    "(source/tests/rig.conf.example)" >&2
	exit 2
}
