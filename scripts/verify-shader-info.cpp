// Unit contract for trustworthy shader header credits and stable note keys.
#include <cstdio>
#include <cstring>
#include <string>
#include <iostream>
#include "shader/shader_info.h"
int main(int argc,char**argv) {
 if(argc!=5)return 2;
 hs_shader_info info{};
 bool shabon=hs_shader_info_read(argv[1], &info);
 bool title=std::string(info.description).find("MIT-licensed GLSL port")!=std::string::npos;
 bool attribution=std::string(info.credit).find("Wrennly")!=std::string::npos;
 bool upstream=std::string(info.source).find("https://github.com/wrennly/shabon-fx/")!=std::string::npos;
 bool license=std::string(info.license).find("MIT")!=std::string::npos;
 hs_shader_info missing{};
 bool absent=hs_shader_info_read(argv[2],&missing);
 bool uncredited=std::string(missing.credit).find("Wrennly")==std::string::npos;
 hs_shader_info authored{};
 bool authoredOk=hs_shader_info_read(argv[4],&authored);
 bool distinctAuthor=std::string(authored.author)=="Artist One";
 bool distinctCredits=std::string(authored.credit)=="Concept by Artist Two";
 bool authoredLicense=std::string(authored.license)=="CC-BY-4.0";
 bool spoofRejected=std::string(authored.author).find("Forged") == std::string::npos;
 bool noteFromAuthor=std::string(authored.notes)=="Use linear-color blending.";
 bool notForged=std::string(authored.credit)=="Concept by Artist Two";
 bool valid=shabon && title && attribution && upstream && license &&
            absent && uncredited && authoredOk && distinctAuthor &&
            distinctCredits && authoredLicense && spoofRejected &&
            noteFromAuthor && notForged;
 std::cout<<"SHADER_INFORMATION_METADATA shabon="<<shabon
          <<" description="<<title<<" credit="<<attribution
          <<" source="<<upstream<<" license="<<license
          <<" unknown_not_forged="<<uncredited
          <<" distinct_author="<<distinctAuthor
          <<" distinct_credits="<<distinctCredits
          <<" authored_notes="<<noteFromAuthor
          <<" false_author_rejected="<<spoofRejected
          <<" PASS="<<valid<<"\n";
 return valid?0:1;
}
