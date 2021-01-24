/* radare - LGPL - Copyright 2019-2020 pancake */

#include "r_lib.h"
#include "r_core.h"
#include "r_lang.h"

static const char *r2v_sym = "r2v__entry";

static int lang_v_file(RLang *lang, const char *file);

static const char *r2v_head = \
	"module r2v\n"
//	"import r2.pipe\n"
	"\n";

static const char *r2v_body = \
	"#pkgconfig --cflags --libs r_core\n"
	"\n"
	"#include <r_core.h>\n"
	"\n"
	"struct R2 {}\n"
	"fn C.r_core_cmd_str (core &R2, s string) byteptr\n"
	"fn C.r_core_free (core &R2)\n"
	"fn C.r_core_new () &R2\n"
	"\n"
	"pub fn (core &R2)cmd(s string) string {\n"
	"  unsafe {\n"
	"    o := C.r_core_cmd_str (core, s.str)\n"
	"    strs := o.vstring()\n"
	"    free(o)\n"
	"    return strs\n"
	"  }\n"
	"}\n"
	"\n"
	"pub fn (core &R2)str() string {\n"
	"        return i64(core).str()\n"
	"}\n"
	"\n"
	"pub fn (core &R2)free() {\n"
	"        unsafe {C.r_core_free (core)}\n"
	"}\n"
	"\n"
	"fn new() &R2 {\n"
	"        return C.r_core_new ()\n"
	"}\n";

typedef struct VParse {
	RStrBuf *head;
	RStrBuf *body;
} VParse;

static void vcode_fini(VParse *p) {
	r_strbuf_free (p->head);
	r_strbuf_free (p->body);
}

static VParse vcode_parse(const char *code) {
	VParse vp = {0};
	vp.head = r_strbuf_new ("");
	vp.body = r_strbuf_new ("");
	size_t i;
	char *c = strdup (code);
	char *p = c;
	char *cp = c;
	for (; *cp; cp++) {
		if (*cp == '\n') {
			*cp = 0;
			if (r_str_startswith (p, "module")) {
				// ignore r_strbuf_appendf (vp.head, "%s\n", p);
			} else if (r_str_startswith (p, "import")) {
				if (strchr (p, '(')) {
					c[i] = '\n';
				}
				char *end = strchr (p, ')');
				if (end) {
					*end = 0;
					cp = end + 1;
				}
				r_strbuf_appendf (vp.head, "%s\n", p);
			} else {
				r_strbuf_appendf (vp.body, "%s\n", p);
			}
			p = cp + 1;
		}
	}
	//r_strbuf_appendf (vp.body, "%s\n", p);
	free (c);
	return vp;
}

static int __run(RLang *lang, const char *code, int len) {
	FILE *fd = r_sandbox_fopen (".tmp.v", "w");
	if (fd) {
		VParse vcode = vcode_parse (code);
		fputs (r2v_head, fd);
		fputs (r_strbuf_get (vcode.head), fd);
		fputs (r2v_body, fd);
		fputs (r_strbuf_get (vcode.body), fd);
		fclose (fd);
		lang_v_file (lang, ".tmp.v");
		r_file_rm (".tmp.v");
		vcode_fini (&vcode);
		return true;
	}
	eprintf ("Cannot open .tmp.v\n");
	return false;
}

static int lang_v_file(RLang *lang, const char *file) {
	if (!r_str_endswith (file, ".v")) {
		return false;
	}
	if (strcmp (file, ".tmp.v")) {
		char *code = r_file_slurp (file, NULL);
		int r = __run (lang, code, -1);
		free (code);
		return r;
	}
	if (!r_file_exists (file)) {
		eprintf ("file not found (%s)\n", file);
		return false;
	}
	char *name = strdup (file);
	char *a = (char*)r_str_lchr (name, '/');
	const char *libpath, *libname;
	if (a) {
		*a = 0;
		libpath = name;
		libname = a + 1;
	} else {
		libpath = ".";
		libname = name;
	}
	r_sys_setenv ("PKG_CONFIG_PATH", R2_LIBDIR"/pkgconfig");
	char *lib = r_str_replace (strdup (file), ".v", "."R_LIB_EXT, 1);
	char *buf = r_str_newf ("v -shared %s", file);
	free (name);
	if (r_sandbox_system (buf, 1) != 0) {
		free (buf);
		free (lib);
		return false;
	}
	free (buf);
	void *vl = r_lib_dl_open (lib);
	if (vl) {
		void (*fcn)(RCore *, int argc, const char **argv);
		fcn = r_lib_dl_sym (vl, r2v_sym);
		if (fcn) {
			fcn (lang->user, 0, NULL);
		} else {
			eprintf ("Cannot find '%s' symbol in library\n", r2v_sym);
		}
		r_lib_dl_close (vl);
	} else {
		eprintf ("Cannot open '%s' library\n", lib);
	}
	r_file_rm (lib);
	free (lib);
	return 0;
}

static int lang_v_run(RLang *lang, const char *code, int len) {
	return __run (lang, code, len);
}

static RLangPlugin r_lang_plugin_v = {
	.name = "v",
	.ext = "v",
	.desc = "V language extension",
	.license = "LGPL",
	.run = lang_v_run,
	.run_file = (void*)lang_v_file,
};

#ifndef CORELIB
struct r_lib_struct_t radare_plugin = {
	.type = R_LIB_TYPE_LANG,
	.data = &r_lang_plugin_v,
};
#endif