/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "../native/pw_launcher_settings.h"
#include "../src/pw_launcher_render.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
static const char original[] =
"; preserve comment\n[application]\nid=needle\nname=Needle\nexecutable=C:\\Game\\game.exe\nworking_directory=C:\\Game\n"
"prefix=needle\nruntime=wine-wow64\narchitecture=pe64\ngraphics=dxvk\n"
"arguments=-force-d3d11 -screen-width 1280 -screen-height 720 -logFile \"C:\\path with spaces\\log.txt\"\n"
"[display]\ndesktop=1280x720\nshow_fps=true\n[input]\nmode=keyboard\nmouse=right_stick\nleft_stick=wasd\nmouse_speed=900\ncross=space\n[runtime]\nshared_input=true\n";
static void test_settings_regressions(const char *root)
{
    char text[8193],out[8193];
    snprintf(text,sizeof(text),"%s",original);
    char *input=strstr(text,"[input]");assert(input);memcpy(input,"[Input]",7);
    char *button=strstr(text,"cross=space");assert(button);memcpy(button,"CROSS",5);
    PwWineLibraryEntry e={0};strcpy(e.file,"needle.profile");
    assert(pw_game_profile_parse((const uint8_t*)text,strlen(text),&e.profile)==PW_OK);
    PwLauncherSettings s;
    assert(!pw_settings_open(&s,&e,root));
    assert(!pw_settings_text(&s,text,out,sizeof(out)));
    assert(strstr(out,"cross = space") && !strstr(out,"CROSS="));
    /* Switching to USB-only must replace inherited/new stick options too. */
    s.input.keyboard_stick=PW_GAME_STICK_RIGHT;s.input.stick_keys[0]='Z';s.input.rumble=35;
    s.row=1;s.mode=3;pw_settings_change(&s,1);
    assert(s.mode==4);
    assert(!pw_settings_text(&s,text,out,sizeof(out)));
    PwGameProfile parsed;
    assert(pw_game_profile_parse((const uint8_t*)out,strlen(out),&parsed)==PW_OK);
    assert(parsed.input.keyboard_stick==PW_GAME_STICK_NONE && !parsed.input.left_stick_wasd);
    assert(parsed.input.stick_keys[0]=='W' && parsed.input.rumble==100);
    /* A missing preset cannot be turned into permanent fallback bindings. */
    strcpy(e.profile.input.preset,"missing");
    assert(pw_settings_open(&s,&e,root)==-1 && !s.open);
}

int main(int argc,char **argv)
{
    char root[]="/tmp/pw-settings-XXXXXX",path[256],out[8193];assert(mkdtemp(root));
    snprintf(path,sizeof(path),"%s/profiles",root);assert(!mkdir(path,0700));
    test_settings_regressions(root);
    PwWineLibraryEntry e={0};strcpy(e.file,"needle.profile");
    assert(pw_game_profile_parse((const uint8_t*)original,strlen(original),&e.profile)==PW_OK);
    PwLauncherSettings s;assert(!pw_settings_open(&s,&e,root));
    assert(s.input.mouse_speed==900&&s.input.left_stick_wasd);
    s.row=0;pw_settings_change(&s,1);pw_settings_change(&s,1);assert(s.draft.display.width==1920);
    pw_settings_change(&s,1);assert(s.draft.display.width==3840&&s.draft.display.height==2160);
    assert(!pw_settings_text(&s,original,out,sizeof(out)));
    assert(strstr(out,"-screen-width 3840 -screen-height 2160"));
    PwGameProfile fourk;assert(pw_game_profile_parse((const uint8_t*)out,strlen(out),&fourk)==PW_OK);
    assert(fourk.display.width==3840&&fourk.display.height==2160);
    pw_settings_change(&s,1);assert(s.draft.display.width==1280);
    pw_settings_change(&s,-1);assert(s.draft.display.width==3840);
    pw_settings_change(&s,-1);assert(s.draft.display.width==1920);
    s.draft.app.graphics=PW_APP_GRAPHICS_GDI;
    pw_settings_change(&s,1);assert(s.draft.display.width==1280);
    pw_settings_change(&s,-1);assert(s.draft.display.width==1920);
    s.draft.app.graphics=PW_APP_GRAPHICS_DXVK;
    s.row=2;pw_settings_change(&s,1);assert(s.input.mouse_speed==1000);
    assert(!pw_settings_text(&s,original,out,sizeof(out)));
    assert(strstr(out,"-screen-width 1920 -screen-height 1080"));
    assert(strstr(out,"\"C:\\path with spaces\\log.txt\""));
    assert(strstr(out,"; preserve comment"));assert(strstr(out,"shared_input=true"));
    assert(strstr(out,"left_stick = wasd"));
    snprintf(path,sizeof(path),"%s/profiles/needle.profile",root);FILE *f=fopen(path,"wb");assert(f);assert(fwrite(original,1,strlen(original),f)==strlen(original));assert(!fclose(f));
    assert(!pw_settings_save(&s,root));
    PwWineLibrary lib;assert(pw_wine_library_load(&lib,root)==PW_OK&&lib.count==1&&lib.entries[0].status==PW_OK);
    assert(lib.entries[0].profile.display.width==1920);
    assert(lib.entries[0].profile.input.mouse_speed==1000);
    s.row=1;pw_settings_change(&s,1);assert(s.input.left_stick_wasd);pw_settings_change(&s,1);assert(s.input.mouse==PW_GAME_STICK_LEFT&&!s.input.left_stick_wasd);pw_settings_change(&s,1);assert(s.input.mode==PW_GAME_INPUT_XINPUT&&s.input.mouse==PW_GAME_STICK_NONE);
    s.row=3;pw_settings_change(&s,1);assert(s.input.bindings[0].kind==PW_GAME_BIND_MOUSE);assert(!pw_settings_save(&s,root));
    snprintf(path,sizeof(path),"%s/profiles/needle.profile.settings-backup",root);f=fopen(path,"rb");assert(f);size_t n=fread(out,1,sizeof(out)-1,f);fclose(f);out[n]=0;assert(!strcmp(out,original));unlink(path);
    snprintf(path,sizeof(path),"%s/profiles/needle.profile",root);unlink(path);assert(pw_settings_save(&s,root)==-1);
    snprintf(path,sizeof(path),"%s/profiles",root);rmdir(path);rmdir(root);
    if(argc>1){
        size_t bytes=1920u*1080*4;uint8_t *pixels=calloc(1,bytes);assert(pixels);
        PwPresentTarget t={pixels,1920,1080,1920*4,bytes};
        s.row=0;pw_settings_labels(&s);
        assert(pw_launcher_render_settings("Needle In A Haystack",s.labels,s.values,21,0,s.status,&t)==PW_OK);
        f=fopen(argv[1],"wb");assert(f);fprintf(f,"P6\n1920 1080\n255\n");
        for(size_t i=0;i<1920u*1080;i++){fputc(pixels[4*i+2],f);fputc(pixels[4*i+1],f);fputc(pixels[4*i],f);}fclose(f);
        if(argc>2) {
            PwLauncherItem item={"Needle In A Haystack","1920x1080",1};
            PwLauncherScene scene={&item,1,0,"Ready"};
            assert(pw_launcher_render(&scene,&t)==PW_OK);
            f=fopen(argv[2],"wb");assert(f);fprintf(f,"P6\n1920 1080\n255\n");
            for(size_t i=0;i<1920u*1080;i++){fputc(pixels[4*i+2],f);fputc(pixels[4*i+1],f);fputc(pixels[4*i],f);}fclose(f);
        }
        free(pixels);
    }
    puts("settings: save/reload, backup, bindings, resolution, arguments and failure paths passed");
}
