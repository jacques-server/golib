/*
 * Copyright (C) 2015 Wiky L
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.";
 */
#include "jmod.h"


static JList *jacques_modules = NULL;

JList *get_jacques_modules(void) {
    return jacques_modules;
}

static inline void register_module(JacModule *mod) {
    if(mod==NULL) {
        return;
    }
    jacques_modules = j_list_append(jacques_modules, mod);
    register_server_init(mod->hooks->init);
    register_client_accept(mod->hooks->accept);
    register_client_recv(mod->hooks->recv);
    register_client_send(mod->hooks->send);
}


/* 从模块中读取模块结构 */
JacModule *jac_loads_module(const char *filename) {
    JModule *mod = j_module_open(filename, J_MODULE_NODELETE|J_MODULE_LAZY);
    if(mod==NULL) {
        return NULL;
    }
    void *ptr=NULL;
    JacModule *result = NULL;
    if(!j_module_symbol(mod, JACQUES_MODULE_NAME, &ptr)||ptr==NULL) {
        goto OUT;
    }
    result = (JacModule*)*((JacModule**)ptr);
OUT:
    j_module_close(mod);
    register_module(result);
    return result;
}


boolean jac_loads_modules(JList *filenames) {
    JList *ptr=filenames;
    while(ptr) {
        JacModule *mod = jac_loads_module((const char*)j_list_data(ptr));
        if(mod==NULL) {
            return FALSE;
        }
        ptr=j_list_next(ptr);
    }
    return TRUE;
}
