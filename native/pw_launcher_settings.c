/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "pw_launcher_settings.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
static const char *buttons[17] = {"cross","circle","square","triangle","l1","r1","l2","r2","l3","r3","up","down","left","right","options","create","touchpad"};
static const char *names[17] = {"CROIX","ROND","CARRE","TRIANGLE","L1","R1","L2","R2","L3","R3","FLECHE HAUT","FLECHE BAS","FLECHE GAUCHE","FLECHE DROITE","OPTIONS","CREATE","PAVE TACTILE"};
static const char *modes[] = {"PERSONNALISE", "DEUX STICKS (WASD + SOURIS)", "SOURIS (STICK GAUCHE)", "MANETTE XINPUT", "CLAVIER / SOURIS USB"};
static const char *keys[] = {"none","mouse_left","mouse_right","mouse_middle","space","enter","escape","shift","ctrl","alt","tab","backspace","up","down","left","right","e","f","r","v","t","w","a","s","d","q","z","1","2","3","4","5","f1","f5","f9"};
static const unsigned widths[] = {1280,1600,1920,3840}, heights[] = {720,900,1080,2160};
static void binding_text(const PwGameBinding *b, char *out, size_t cap)
{
    if (b->kind == PW_GAME_BIND_MOUSE) snprintf(out,cap,"%s",b->code==0?"mouse_left":b->code==1?"mouse_right":"mouse_middle");
    else if (b->kind != PW_GAME_BIND_KEY) snprintf(out,cap,"none");
    else if (b->code >= 'A' && b->code <= 'Z') snprintf(out,cap,"%c",(char)(b->code+32));
    else if (b->code >= '0' && b->code <= '9') snprintf(out,cap,"%c",(char)b->code);
    else {
        const struct { unsigned code; const char *name; } map[]={{32,"space"},{13,"enter"},{27,"escape"},{16,"shift"},{17,"ctrl"},{18,"alt"},{9,"tab"},{8,"backspace"},{38,"up"},{40,"down"},{37,"left"},{39,"right"}};
        for (size_t i=0;i<sizeof(map)/sizeof(map[0]);i++) if(b->code==map[i].code){snprintf(out,cap,"%s",map[i].name);return;}
        if(b->code>=0x70 && b->code<=0x87)snprintf(out,cap,"f%u",b->code-0x6f);
        else snprintf(out,cap,"vk:0x%02X",b->code);
    }
}
void pw_settings_labels(PwLauncherSettings *s)
{
    snprintf(s->labels[0],32,"RESOLUTION");
    if(s->draft.display.width)snprintf(s->values[0],64,"%u X %u%s",s->draft.display.width,s->draft.display.height,s->draft.display.width==3840?" (4K)":"");
    else snprintf(s->values[0],64,"AUTOMATIQUE");
    snprintf(s->labels[1],32,"COMMANDES");snprintf(s->values[1],64,"%s",modes[s->mode]);
    snprintf(s->labels[2],32,"SENSIBILITE SOURIS");snprintf(s->values[2],64,"%u",s->input.mouse_speed);
    for(int i=0;i<17;i++){snprintf(s->labels[i+3],32,"%s",names[i]);binding_text(&s->input.bindings[i],s->values[i+3],64);}
    for(int i=3;i<20;i++) {
        const struct { const char *key,*label; } translations[] = {
            {"none","AUCUN"},{"mouse_left","CLIC GAUCHE"},{"mouse_right","CLIC DROIT"},
            {"mouse_middle","CLIC CENTRAL"},{"space","ESPACE"},{"enter","ENTREE"},
            {"escape","ECHAP"},{"shift","MAJ"},{"backspace","RETOUR ARRIERE"},
            {"up","HAUT"},{"down","BAS"},{"left","GAUCHE"},{"right","DROITE"}
        };
        for(size_t k=0;k<sizeof(translations)/sizeof(translations[0]);k++)
            if(!strcmp(s->values[i],translations[k].key)) {
                snprintf(s->values[i],64,"%s",translations[k].label);break;
            }
    }
    snprintf(s->labels[20],32,"ENREGISTRER ET RETOUR");s->values[20][0]=0;
}
int pw_settings_open(PwLauncherSettings *s,const PwWineLibraryEntry *e,const char *root)
{
    if(!s||!e||e->status!=PW_OK)return -1;
    memset(s,0,sizeof(*s));s->draft=e->profile;snprintf(s->file,sizeof(s->file),"%s",e->file);
    if (pw_wine_library_input(&e->profile,root,&s->input) != PW_OK) return -1;
    s->open=1;
    snprintf(s->status,sizeof(s->status),"MODIFICATIONS APPLIQUEES AU PROCHAIN LANCEMENT");
    pw_settings_labels(s);return 0;
}
void pw_settings_change(PwLauncherSettings *s,int d)
{
    if(!s||!s->open||!d)return;
    if(s->row==0){
        int count=(s->draft.app.graphics==PW_APP_GRAPHICS_DXVK || s->draft.app.graphics==PW_APP_GRAPHICS_ZINK)?4:3;
        int i=-1;for(int n=0;n<count;n++)if(s->draft.display.width==widths[n]&&s->draft.display.height==heights[n])i=n;
        i=i<0?0:(i+(d>0?1:count-1))%count;s->draft.display.width=widths[i];s->draft.display.height=heights[i];s->resolution_changed=1;
        snprintf(s->status,sizeof(s->status),"%s",i==3?"4K: ECRAN ET SORTIE PS5 2160P REQUIS - FLUIDITE A TESTER":"MODIFICATIONS APPLIQUEES AU PROCHAIN LANCEMENT");
    }else if(s->row==1){
        s->mode=s->mode==0?(d>0?1:4):1+(s->mode-1+(d>0?1:3))%4;
        unsigned speed=s->input.mouse_speed;
        pw_game_input_init(&s->input);
        s->input.mouse_speed=speed;
        const char *layout=s->mode==1?
            "[input]\nmode=keyboard\nleft_stick=wasd\nmouse=right_stick\ncross=space\ncircle=ctrl\nsquare=e\ntriangle=f\nr2=mouse_left\nl2=mouse_right\nl3=shift\nr1=r\nl1=v\noptions=escape\ntouchpad=enter\n":s->mode==2?
            "[input]\nmode=keyboard\nleft_stick=none\nmouse=left_stick\ncross=mouse_left\nr2=mouse_left\nsquare=mouse_right\ncircle=escape\ntriangle=enter\noptions=escape\n":s->mode==3?
            "[input]\nmode=xinput\nleft_stick=none\nmouse=none\n":"[input]\nmode=keyboard\nleft_stick=none\nmouse=none\n";
        pw_game_input_parse((const uint8_t *)layout,strlen(layout),&s->input);
        s->input.mouse_speed=speed;
    }else if(s->row==2){
        int speed=(int)s->input.mouse_speed+(d>0?100:-100);if(speed<100)speed=100;if(speed>5000)speed=5000;s->input.mouse_speed=(unsigned)speed;
    }else if(s->row>=3&&s->row<20){
        char old[32],line[96];int i=-1,n=(int)(sizeof(keys)/sizeof(keys[0]));
        binding_text(&s->input.bindings[s->row-3],old,sizeof(old));
        for(int k=0;k<n;k++)if(!strcmp(old,keys[k]))i=k;
        i=i<0?0:(i+(d>0?1:n-1))%n;
        snprintf(line,sizeof(line),"[input]\n%s=%s\n",buttons[s->row-3],keys[i]);
        pw_game_input_parse((const uint8_t*)line,strlen(line),&s->input);s->mode=0;
    }
    pw_settings_labels(s);
}
/* Replace one key inside its section, preserving all unrelated text. */
static int set(char *text,size_t cap,const char *section,const char *key,const char *value)
{
    char tmp[PW_APP_PROFILE_MAX_BYTES+1], sec[64];size_t used=0;int inside=0,found=0,seen=0;
    snprintf(sec,sizeof(sec),"[%s]",section);
    const char *p=text;
    while(*p){
        const char *end=strchr(p,'\n');size_t len=end?(size_t)(end-p)+1:strlen(p);
        const char *q=p;while(q<p+len&&(*q==' '||*q=='\t'))q++;
        if(*q=='['){
            if(inside&&!found){int n=snprintf(tmp+used,sizeof(tmp)-used,"%s = %s\n",key,value);if(n<0||(size_t)n>=sizeof(tmp)-used)return -1;used+=(size_t)n;found=1;}
            inside=!strncasecmp(q,sec,strlen(sec));if(inside)seen=1;
        }
        int match=0;
        if(inside&&!strncasecmp(q,key,strlen(key))){const char *v=q+strlen(key);while(*v==' '||*v=='\t')v++;match=*v=='=';}
        if(match){int n=snprintf(tmp+used,sizeof(tmp)-used,"%s = %s\n",key,value);if(n<0||(size_t)n>=sizeof(tmp)-used)return -1;used+=(size_t)n;found=1;}
        else {if(used+len+2>=sizeof(tmp))return -1;memcpy(tmp+used,p,len);used+=len;if(!end)tmp[used++]='\n';}
        p+=len;
    }
    if(!found){int n=snprintf(tmp+used,sizeof(tmp)-used,seen?"%s = %s\n":"[%s]\n",seen?key:section,value);if(n<0||(size_t)n>=sizeof(tmp)-used)return -1;used+=(size_t)n;
        if(!seen){n=snprintf(tmp+used,sizeof(tmp)-used,"%s = %s\n",key,value);if(n<0||(size_t)n>=sizeof(tmp)-used)return -1;used+=(size_t)n;}}
    if(used>=cap)return -1;
    tmp[used]=0;memcpy(text,tmp,used+1);return 0;
}
/* Only rewrite already present Unity resolution switches, never invent them
 * for other engines or rewrite quoted path/argument contents. */
static int resolution_args(char *args,size_t cap,unsigned w,unsigned h)
{
    char out[PW_APP_ARGUMENTS_CAPACITY];size_t used=0;const char *p=args;
    while(*p){const char *start=p;int quoted=0;
        while(*p&&(!isspace((unsigned char)*p)||quoted)){if(*p=='"')quoted=!quoted;p++;}
        size_t len=(size_t)(p-start);int width=len==13&&!memcmp(start,"-screen-width",13),height=len==14&&!memcmp(start,"-screen-height",14);
        if(used+len+1>=sizeof(out))return -1;
        memcpy(out+used,start,len);used+=len;
        if(width||height){while(*p==' '||*p=='\t')p++;const char *n=p;while(isdigit((unsigned char)*p))p++;
            if(p==n||(*p&&!isspace((unsigned char)*p)))return -1;
            int k=snprintf(out+used,sizeof(out)-used," %u",width?w:h);if(k<0||(size_t)k>=sizeof(out)-used)return -1;used+=(size_t)k;}
        while(*p&&isspace((unsigned char)*p)){if(used+1>=sizeof(out))return -1;out[used++]=*p++;}
    }
    if(used>=cap)return -1;
    out[used]=0;memcpy(args,out,used+1);return 0;
}
int pw_settings_text(const PwLauncherSettings *s,const char *original,char *out,size_t cap)
{
    if(!s||!original||strlen(original)>=cap)return -1;
    strcpy(out,original);char val[64],args[PW_APP_ARGUMENTS_CAPACITY];
    if(s->resolution_changed){snprintf(val,sizeof(val),"%ux%u",s->draft.display.width,s->draft.display.height);if(set(out,cap,"display","desktop",val))return -1;
        strcpy(args,s->draft.app.arguments);if(resolution_args(args,sizeof(args),s->draft.display.width,s->draft.display.height)||set(out,cap,"application","arguments",args))return -1;}
    if(set(out,cap,"input","mode",s->input.mode==PW_GAME_INPUT_XINPUT?"xinput":"keyboard")||
       set(out,cap,"input","left_stick",s->input.left_stick_wasd?"wasd":"none")||
       set(out,cap,"input","mouse",s->input.mouse==PW_GAME_STICK_LEFT?"left_stick":s->input.mouse==PW_GAME_STICK_RIGHT?"right_stick":"none"))return -1;
    snprintf(val,sizeof(val),"%u",s->input.mouse_speed);if(set(out,cap,"input","mouse_speed",val))return -1;
    if(set(out,cap,"input","keyboard_stick",s->input.keyboard_stick==PW_GAME_STICK_LEFT?"left_stick":s->input.keyboard_stick==PW_GAME_STICK_RIGHT?"right_stick":"none"))return -1;
    snprintf(val,sizeof(val),"%u",s->input.stick_deadzone);if(set(out,cap,"input","stick_deadzone",val))return -1;
    snprintf(val,sizeof(val),"%u",s->input.rumble);if(set(out,cap,"input","rumble",val))return -1;
    const char *directions[4]={"stick_up","stick_down","stick_left","stick_right"};
    for(int i=0;i<4;i++){
        PwGameBinding key={0};key.kind=PW_GAME_BIND_KEY;key.code=s->input.stick_keys[i];
        binding_text(&key,val,sizeof(val));if(set(out,cap,"input",directions[i],val))return -1;
    }
    for(int i=0;i<17;i++){binding_text(&s->input.bindings[i],val,sizeof(val));if(set(out,cap,"input",buttons[i],val))return -1;}
    PwGameProfile check;return pw_game_profile_parse((const uint8_t*)out,strlen(out),&check)==PW_OK?0:-1;
}
static int write_all(int fd,const char *text,size_t n)
{
    while(n){ssize_t k=write(fd,text,n);if(k<0&&errno==EINTR)continue;if(k<=0)return -1;text+=k;n-=(size_t)k;}return 0;
}
int pw_settings_save(PwLauncherSettings *s,const char *root)
{
    char path[384],stage[400],backup[400],old[PW_APP_PROFILE_MAX_BYTES+1],out[PW_APP_PROFILE_MAX_BYTES+1];
    if(snprintf(path,sizeof(path),"%s/profiles/%s",root,s->file)>=(int)sizeof(path))return -1;
    snprintf(stage,sizeof(stage),"%s.settings-tmp",path);snprintf(backup,sizeof(backup),"%s.settings-backup",path);
    int fd=open(path,O_RDONLY);if(fd<0)return -1;size_t n=0;ssize_t k;
    while(n<sizeof(old)-1&&(k=read(fd,old+n,sizeof(old)-1-n))>0)n+=(size_t)k;
    close(fd);if(n==sizeof(old)-1||k<0)return -1;old[n]=0;
    if(pw_settings_text(s,old,out,sizeof(out)))return -1;
    fd=open(backup,O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd>=0){int bad=write_all(fd,old,n);if(close(fd))bad=1;if(bad){unlink(backup);return -1;}}
    else if(errno!=EEXIST)return -1;
    fd=open(stage,O_WRONLY|O_CREAT|O_TRUNC,0600);if(fd<0)return -1;
    int bad=write_all(fd,out,strlen(out));if(close(fd))bad=1;
    if(bad){unlink(stage);return -1;}
    if(rename(stage,path)){unlink(stage);return -1;}
    (void)pw_game_profile_parse((const uint8_t*)out,strlen(out),&s->draft);
    return 0;
}
