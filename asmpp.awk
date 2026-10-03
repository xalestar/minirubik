# Minimal assembler preprocessor for Ripes, whose assembler has no .if or
# .include. It handles ".if NAME", ".else", ".endif" and '.include "file"',
# and passes every other line, comments included, through unchanged.
# NAME is a value set with -v NAME=1 on the command line, or else the
# last ".equ NAME, value" seen above it.
#   awk -v RENDER=0 -f asmpp.awk rubik.s > rubik-cli.s

function emit(line,    f, a, name, sub_line) {
    if (line ~ /^[ \t]*\.if[ \t]/) {
        name = line
        sub(/^[ \t]*\.if[ \t]+/, "", name)
        sub(/[ \t]*(#.*)?$/, "", name)
        depth++
        live[depth] = live[depth - 1] && value(name) != 0
        return
    }
    if (line ~ /^[ \t]*\.else([ \t#]|$)/) {
        live[depth] = live[depth - 1] && !live[depth]
        return
    }
    if (line ~ /^[ \t]*\.endif([ \t#]|$)/) {
        depth--
        return
    }
    if (!live[depth])
        return
    if (line ~ /^[ \t]*\.include[ \t]/) {
        split(line, a, "\"")
        f = a[2]
        while ((getline sub_line < f) > 0)
            emit(sub_line)
        close(f)
        return
    }
    if (line ~ /^[ \t]*\.equ[ \t]/) {
        split(line, a, /[ \t,]+/)
        if (a[2] in cli)
            line = ".equ " a[2] ", " cli[a[2]]
        else
            equ[a[2]] = a[3] + 0
    }
    print line
}

function value(name) {
    if (name in cli)
        return cli[name]
    if (name in equ)
        return equ[name]
    print "asmpp: undefined " name > "/dev/stderr"
    exit 1
}

BEGIN {
    depth = 0
    live[0] = 1
    if (RENDER != "")
        cli["RENDER"] = RENDER + 0
}

{ emit($0) }

END {
    if (depth) {
        print "asmpp: unterminated .if" > "/dev/stderr"
        exit 1
    }
}
