#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <util/dstr.h>
#include <util/bmem.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>
#ifdef HS_TRANSLATOR_BASELINE
#include "baseline.inc"
#else
#include "shader/glsl-translate.h"
#endif
static unsigned failures;
static void check(const char *name,const char *in,const char *expected)
{
    char *result=translate_glsl_to_obs((char *)in);
    if (!result || strcmp(result,expected)) {
        fprintf(stderr,"FAIL %s: actual=[%s] expected=[%s]\n",name,result?result:"NULL",expected);
        ++failures;
    }
    bfree(result);
}
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"bench")){
        const char *line="vec2 uv=fract(vec2(0.1,0.2)); vec4 v=mix(vec4(uv,0,1),vec4(1,2,3,4),mod(1.0,0.7)); float untouched=0.1;\n";
        struct dstr input={0};
        for(int i=0;i<3000;i++)dstr_cat(&input,line);
        clock_t start=clock();
        size_t total=0;
        for(int i=0;i<20;i++){
            char *result=translate_glsl_to_obs(input.array);
            total+=strlen(result);bfree(result);
        }
        printf("TRANSLATE_BENCH input=%zu iterations=20 output=%zu cpu_ms=%.2f\n",input.len,total,1000.0*(clock()-start)/CLOCKS_PER_SEC);
        char *sample=translate_glsl_to_obs(input.array);
        uint64_t hash=UINT64_C(14695981039346656037);
        for(const unsigned char *p=(const unsigned char *)sample;*p;p++){
            hash^=*p;hash*=UINT64_C(1099511628211);
        }
        printf("TRANSLATE_HASH %016" PRIx64 "\n",hash);
        bfree(sample);dstr_free(&input);return 0;
    }
    check("mapping","vec2 vec3 vec4 ivec2 ivec3 ivec4 mat2 mat3 mat4 sampler2D fract mix mod",
          "float2 float3 float4 int2 int3 int4 float2x2 float3x3 float4x4 texture2d frac lerp fmod\n");
    check("boundaries","vec2ish _vec2 vec2_ 2vec2 vec2 vec2x mix2 remix MOD mod(x);","vec2ish _vec2 vec2_ 2vec2 float2 vec2x mix2 remix MOD fmod(x);\n");
    check("comments","vec2 v; // mix vec4\n/* mat2\nfract */ vec3 x;\n","float2 v; // mix vec4\n/* mat2\nfract */ float3 x;\n");
    check("quotes","\"mix \\\" vec2\" 'mod' vec4 x;\n","\"mix \\\" vec2\" 'mod' float4 x;\n");
    check("directives","  #define vec2 float2\n#define F(x) mix(x, \\\n vec2(0,0))\nvec2 p;\n","  #define vec2 float2\n#define F(x) mix(x, \\\n vec2(0,0))\nfloat2 p;\n");
    check("comment directive","/* vec2 */ #define mix lerp\nvec2 x;\n","/* vec2 */ #define mix lerp\nfloat2 x;\n");
    check("CRLF continuation","#define M \\\r\n vec2\r\nvec2 x;\r\n","#define M \\\r\n vec2\r\nfloat2 x;\r\n");
    check("directive comment","#define M /* vec2 */ mix(vec2(0,0))\nvec2 x;\n","#define M /* vec2 */ mix(vec2(0,0))\nfloat2 x;\n");
    check("directive string","#define M \"/* vec2\" mix\nvec2 x;\n","#define M \"/* vec2\" mix\nfloat2 x;\n");
    check("multiline directive comment","#define M /* vec2\n mix */ vec2 x;\n","#define M /* vec2\n mix */ vec2 x;\n");
    if(translate_glsl_to_obs(NULL)!=NULL)++failures;
    check("empty","","");
    check("trailing","float2 x;\n\n","float2 x;\n\n");
    check("unfinished comment","vec2 /* mix","float2 /* mix\n");
    check("line comment continuation","// vec2 \\\n mix\nvec3 x;","// vec2 \\\n mix\nfloat3 x;\n");
    printf("GLSL_TRANSLATION failures=%u PASS=%d\n",failures,failures==0);
    return failures?1:0;
}
