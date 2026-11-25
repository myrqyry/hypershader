#pragma once
// Read the real native property schema; only explicitly bounded artist
// float sliders participate in looks. Never touch source routing or modes.
#include <obs.h>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <random>
struct HSPerformanceField {QString key;double min,max,step;};
using HSLook=QMap<QString,double>;
inline void hsCollectPerformanceFields(obs_properties_t*p,QVector<HSPerformanceField>&out){
 for(auto*v=obs_properties_first(p);v;obs_property_next(&v)){
  if(!obs_property_visible(v)||!obs_property_enabled(v))continue;
  auto type=obs_property_get_type(v);
  if(type==OBS_PROPERTY_GROUP){hsCollectPerformanceFields(obs_property_group_content(v),out);continue;}
  QString key=QString::fromUtf8(obs_property_name(v));
  if(!key.startsWith("param_")||type!=OBS_PROPERTY_FLOAT||obs_property_float_type(v)!=OBS_NUMBER_SLIDER)continue;
  double lo=obs_property_float_min(v),hi=obs_property_float_max(v),step=obs_property_float_step(v);
  if(std::isfinite(lo)&&std::isfinite(hi)&&std::isfinite(step)&&lo<hi&&step>0&&lo>-1000&&hi<1000)
   out.push_back({key,lo,hi,step});
 }
}
inline QVector<HSPerformanceField> hsPerformanceFields(obs_properties_t*p){
 QVector<HSPerformanceField> out;if(p)hsCollectPerformanceFields(p,out);return out;
}
inline double hsBound(const HSPerformanceField&f,double v){
 if(!std::isfinite(v))return f.min;
 return std::clamp(v,f.min,f.max);
}
inline HSLook hsCaptureLook(const QVector<HSPerformanceField>&fields,obs_data_t*s){
 HSLook out;for(const auto&f:fields)out[f.key]=hsBound(f,obs_data_get_double(s,f.key.toUtf8().constData()));return out;
}
inline HSLook hsVaryLook(const QVector<HSPerformanceField>&fields,const HSLook&a,const QSet<QString>&locks,double strength,unsigned seed){
 HSLook out=a;std::mt19937 rng(seed);std::uniform_real_distribution<double> unit(-1.,1.);
 strength=std::clamp(strength,0.,1.);
 for(const auto&f:fields)if(a.contains(f.key)&&!locks.contains(f.key)&&strength>0){
  double v=a[f.key]+unit(rng)*(f.max-f.min)*strength;
  out[f.key]=hsBound(f,f.min+std::round((v-f.min)/f.step)*f.step);
 }return out;
}
inline HSLook hsMorphLook(const QVector<HSPerformanceField>&fields,const HSLook&a,const HSLook&b,double t){
 HSLook out;t=std::clamp(t,0.,1.);
 for(const auto&f:fields)if(a.contains(f.key)&&b.contains(f.key))
  out[f.key]=hsBound(f,a[f.key]+(b[f.key]-a[f.key])*t);
 return out;
}
inline void hsWriteLook(const QVector<HSPerformanceField>&fields,obs_data_t*s,const HSLook&look){
 for(const auto&f:fields)if(look.contains(f.key)&&std::isfinite(look[f.key]))
  obs_data_set_double(s,f.key.toUtf8().constData(),hsBound(f,look[f.key]));
}
