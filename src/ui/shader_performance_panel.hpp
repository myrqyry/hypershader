#pragma once
#include "shader_performance_model.hpp"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QScrollArea>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QLineEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QScrollBar>
#include <QVariant>
#include <QAbstractSpinBox>
#include <QColorDialog>
#include <QTimer>
#include <QRandomGenerator>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QApplication>
#include <QPointer>
#include <QDataStream>
#include <QIODevice>
#include <vector>
#include <QProgressBar>
#include <callback/calldata.h>
#include <callback/proc.h>
#include <functional>
#include <memory>

class ShaderPerformancePanel : public QWidget {
public:
 using Target=std::function<QString()>;
 explicit ShaderPerformancePanel(Target target,QWidget*parent=nullptr):QWidget(parent),targetUuid(std::move(target)){
  auto*l=new QVBoxLayout(this);l->setContentsMargins(6,6,6,6);l->setSpacing(4);
  status=new QLabel("Select a hypershader instance above.",this);status->setWordWrap(true);
  status->setTextFormat(Qt::PlainText);status->setObjectName("hsPerformanceStatus");l->addWidget(status);
  signalLabel=new QLabel(this);signalLabel->setTextFormat(Qt::PlainText);signalLabel->setWordWrap(true);signalLabel->setObjectName("hsPerformanceSignals");l->addWidget(signalLabel);
  meter=new QProgressBar(this);meter->setRange(0,1000);meter->setTextVisible(false);meter->setFixedHeight(6);meter->hide();l->addWidget(meter);
  auto*transport=new QHBoxLayout;
  enabled=new QPushButton("Bypass",this);enabled->setCheckable(true);enabled->setObjectName("hsPerformanceBypass");
  freeze=new QPushButton("Freeze time",this);freeze->setCheckable(true);freeze->setObjectName("hsPerformanceFreeze");
  refreshButton=new QPushButton("Refresh",this);
  transport->addWidget(enabled);transport->addWidget(freeze);transport->addWidget(refreshButton);l->addLayout(transport);
  connect(enabled,&QPushButton::clicked,this,[this](bool bypass){withTarget([&](obs_source_t*src,obs_data_t*){obs_source_set_enabled(src,!bypass);});refresh(true);});
  connect(freeze,&QPushButton::clicked,this,[this](bool paused){withTarget([&](obs_source_t*src,obs_data_t*s){
    if(paused){resumeSpeed=obs_data_get_double(s,"speed");obs_data_set_double(s,"speed",0);}
    else obs_data_set_double(s,"speed",resumeSpeed>0?resumeSpeed:1.0);
    obs_source_update(src,s);
  });refresh(true);});
  connect(refreshButton,&QPushButton::clicked,this,[this]{refresh(true);});
  looks=new QGroupBox("Variations · bounded shader controls",this);auto*vl=new QVBoxLayout(looks);vl->setContentsMargins(5,7,5,5);vl->setSpacing(4);
  auto*vr=new QHBoxLayout;auto*random=new QPushButton("Vary",looks);random->setObjectName("hsPerformanceVary");
  strength=new QSlider(Qt::Horizontal,looks);strength->setRange(0,100);strength->setValue(25);strength->setToolTip("Variation amount (% of declared range)");
  auto*strengthLabel=new QLabel("25%",looks);connect(strength,&QSlider::valueChanged,this,[strengthLabel](int v){strengthLabel->setText(QString::number(v)+"%");});
  undoButton=new QPushButton("Undo",looks);undoButton->setObjectName("hsPerformanceUndo");
  vr->addWidget(random);vr->addWidget(strength,1);vr->addWidget(strengthLabel);vr->addWidget(undoButton);vl->addLayout(vr);
  connect(random,&QPushButton::clicked,this,[this]{withTarget([&](obs_source_t*src,obs_data_t*s){auto a=hsCaptureLook(fields,s);undo=a;hsWriteLook(fields,s,hsVaryLook(fields,a,locks,strength->value()/100.,QRandomGenerator::global()->generate()));obs_source_update(src,s);});refresh(true);});
  connect(undoButton,&QPushButton::clicked,this,[this]{withTarget([&](obs_source_t*src,obs_data_t*s){auto previous=hsCaptureLook(fields,s);hsWriteLook(fields,s,undo);undo=previous;obs_source_update(src,s);});refresh(true);});
  auto*ab=new QHBoxLayout;auto*a=new QPushButton("Set A",looks);auto*b=new QPushButton("Set B",looks);
  a->setObjectName("hsPerformanceCaptureA");b->setObjectName("hsPerformanceCaptureB");
  morph=new QSlider(Qt::Horizontal,looks);morph->setRange(0,100);morph->setObjectName("hsPerformanceMorph");morph->setEnabled(false);
  ab->addWidget(a);ab->addWidget(morph,1);ab->addWidget(b);vl->addLayout(ab);
  connect(a,&QPushButton::clicked,this,[this]{withTarget([&](obs_source_t*,obs_data_t*s){lookA=hsCaptureLook(fields,s);});syncLooks();});
  connect(b,&QPushButton::clicked,this,[this]{withTarget([&](obs_source_t*,obs_data_t*s){lookB=hsCaptureLook(fields,s);});syncLooks();});
  connect(morph,&QSlider::valueChanged,this,[this](int value){if(!morph->isSliderDown())applyMorph(value);});
  connect(morph,&QSlider::sliderReleased,this,[this]{applyMorph(morph->value());});
  lookStatus=new QLabel("Lock individual sliders to protect them from variations.",looks);lookStatus->setWordWrap(true);vl->addWidget(lookStatus);l->addWidget(looks);
  scroll=new QScrollArea(this);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);l->addWidget(scroll,1);
  timer=new QTimer(this);timer->setInterval(500);connect(timer,&QTimer::timeout,this,[this]{if(isVisible())refresh(false);});timer->start();
  setReady(false);
 }
 void refresh(bool force=false){
  if(!isVisible()&&!force)return;
  auto src=resolve();
  if(!src){if(force||!identity.isEmpty()){identity.clear();clearLooks();replaceForm(nullptr,nullptr);formReady=false;setReady(false);status->setText("Select a hypershader instance above.");}enabled->setEnabled(false);signalLabel->clear();meter->hide();return;}
  auto s=settings(src.get());auto p=privateSettings(src.get());
  const QString path=string(s.get(),"shader_file"),state=string(p.get(),"hypershader_live_state");
  const bool ready=!path.isEmpty()&&state=="ready"&&path==string(p.get(),"hypershader_live_path");
  const QString next=targetUuid()+"\n"+path+"\n"+QString::number(obs_data_get_int(p.get(),"hypershader_live_revision"));
  const QString sig=QString::fromUtf8(obs_data_get_json_with_defaults(s.get()));
  status->setText(QFileInfo(path).fileName()+" · "+(ready?(obs_source_showing(src.get())?"Showing":"Not showing"):(state=="failed"?"Compile failed":"Waiting for native compilation")));
  {QSignalBlocker eb(enabled),fb(freeze);enabled->setChecked(!obs_source_enabled(src.get()));freeze->setChecked(obs_data_get_double(s.get(),"speed")==0);}
  enabled->setEnabled(ready);
  calldata_t cd;calldata_init(&cd);
  QStringList active;
  if(ready&&proc_handler_call(obs_source_get_proc_handler(src.get()),"hypershader_live_status",&cd)){
   bool rendering=calldata_bool(&cd,"rendering"),audio=calldata_bool(&cd,"audio");
   unsigned flags=unsigned(calldata_int(&cd,"signals"));
   if(audio)active<<"Audio";
   const char*names[]={"Tracking","Depth","Flow","Text","Motion","Matte","Subject","Face"};
   for(unsigned i=0;i<8;i++)if(flags&(1u<<i))active<<names[i];
   for(unsigned i=0;i<4;i++)if(flags&(1u<<(8+i)))active<<QString("Input %1").arg(i);
   signalLabel->setText(!rendering?"No recent render":active.isEmpty()?"No live input signals":active.join(" · "));
   meter->setVisible(audio);meter->setValue(int(calldata_int(&cd,"level")));
  }else{signalLabel->clear();meter->hide();}
  calldata_free(&cd);
  if(next!=identity){identity=next;clearLooks();signature.clear();formShape.clear();force=true;}
  if(!ready){if(formReady){replaceForm(nullptr,nullptr);formReady=false;}setReady(false);return;}
  if(!force&&formReady&&sig==signature)return;
  // Do not replace a control while its editor has focus or a drag is active.
  auto*focus=QApplication::focusWidget();
  if(!force&&focus&&scroll->isAncestorOf(focus)&&(qobject_cast<QAbstractSpinBox*>(focus)||qobject_cast<QLineEdit*>(focus)))return;
  if(!force)for(auto*slider:scroll->findChildren<QSlider*>())if(slider->isSliderDown())return;
  std::unique_ptr<obs_properties_t,decltype(&obs_properties_destroy)> props(obs_source_properties(src.get()),obs_properties_destroy);
  if(!props){setReady(false);return;}
  fields=hsPerformanceFields(props.get());
  auto shape=shapeOf(props.get(),s.get());
  if(formReady&&shape==formShape){for(const auto&sync:fieldSync)sync(s.get());}
  else {replaceForm(props.get(),s.get());formShape=shape;}
  signature=sig;formReady=true;setReady(true);
 }
private:
 using Source=std::unique_ptr<obs_source_t,decltype(&obs_source_release)>;
 using Data=std::unique_ptr<obs_data_t,decltype(&obs_data_release)>;
 Target targetUuid;
 QString identity,signature;QByteArray formShape;bool formReady=false;double resumeSpeed=1;
 std::vector<std::function<void(obs_data_t*)>> fieldSync;
 QVector<HSPerformanceField> fields;QSet<QString> locks;HSLook lookA,lookB,undo;
 QLabel*status,*lookStatus,*signalLabel;QProgressBar*meter;QPushButton*enabled,*freeze,*refreshButton,*undoButton;QGroupBox*looks;
 QSlider*strength,*morph;QScrollArea*scroll;QTimer*timer;
 static QString string(obs_data_t*s,const char*k){return s?QString::fromUtf8(obs_data_get_string(s,k)):QString();}
 Source resolve(){return Source(obs_get_source_by_uuid(targetUuid().toUtf8().constData()),obs_source_release);}
 static Data settings(obs_source_t*s){return Data(obs_source_get_settings(s),obs_data_release);}
 static Data privateSettings(obs_source_t*s){return Data(obs_source_get_private_settings(s),obs_data_release);}
 void clearLooks(){fields.clear();locks.clear();lookA.clear();lookB.clear();undo.clear();resumeSpeed=1;QSignalBlocker block(morph);morph->setValue(0);syncLooks();}
 void syncLooks(){undoButton->setEnabled(!undo.isEmpty());morph->setEnabled(!lookA.isEmpty()&&!lookB.isEmpty());lookStatus->setText(QString("A %1  ·  B %2  ·  %3 locked").arg(lookA.isEmpty()?"empty":"saved",lookB.isEmpty()?"empty":"saved").arg(locks.size()));}
 void setReady(bool ready){freeze->setEnabled(ready);looks->setEnabled(ready&&!fields.isEmpty());scroll->setEnabled(ready);syncLooks();}
 template<class F>void withTarget(F f){
  auto src=resolve();if(!src||obs_source_removed(src.get()))return;
  auto s=settings(src.get()),p=privateSettings(src.get());const QString path=string(s.get(),"shader_file");
  const QString now=targetUuid()+"\n"+path+"\n"+QString::number(obs_data_get_int(p.get(),"hypershader_live_revision"));
  if(now!=identity||path!=string(p.get(),"hypershader_live_path")||string(p.get(),"hypershader_live_state")!="ready")return;
  f(src.get(),s.get());
 }
 void applyMorph(int value){
  if(lookA.isEmpty()||lookB.isEmpty())return;
  withTarget([&](obs_source_t*src,obs_data_t*s){undo=hsCaptureLook(fields,s);hsWriteLook(fields,s,hsMorphLook(fields,lookA,lookB,value/100.));obs_source_update(src,s);});
  refresh(true);
 }
 void write(const QString&key,const QVariant&value,const QString&token){
  if(token!=identity)return;
  bool changed=false;
  withTarget([&](obs_source_t*src,obs_data_t*s){
   const QByteArray k=key.toUtf8();
   if(value.typeId()==QMetaType::Bool&&obs_data_get_bool(s,k.constData())==value.toBool())return;
   if(value.typeId()==QMetaType::Double&&std::abs(obs_data_get_double(s,k.constData())-value.toDouble())<1e-9)return;
   if((value.typeId()==QMetaType::Int||value.typeId()==QMetaType::LongLong)&&obs_data_get_int(s,k.constData())==value.toLongLong())return;
   if(value.typeId()==QMetaType::QString&&string(s,k.constData())==value.toString())return;
   changed=true;
   switch(value.typeId()){
   case QMetaType::Bool:obs_data_set_bool(s,k.constData(),value.toBool());break;
   case QMetaType::Double:obs_data_set_double(s,k.constData(),value.toDouble());break;
   case QMetaType::LongLong:case QMetaType::Int:obs_data_set_int(s,k.constData(),value.toLongLong());break;
   default:obs_data_set_string(s,k.constData(),value.toString().toUtf8().constData());break;
   }
   // Explicit new channel selection must not keep an obsolete legacy UUID.
   if(key.startsWith("channel")&&key.endsWith("_source"))
    obs_data_set_string(s,(key.left(key.size()-7)+"_uuid").toUtf8().constData(),"");
   obs_source_update(src,s);
  });
  if(changed)QTimer::singleShot(0,this,[this]{refresh(true);});
 }
 // Compare the live native property schema, not widget identity or values.
 // Rebuild only for a different target/revision, a changed conditional schema,
 // or a changed modulation binding. Regular values are synchronized in place.
 static void appendShape(QDataStream&out,obs_properties_t*p){
  for(auto*v=obs_properties_first(p);v;obs_property_next(&v)){
   if(!obs_property_visible(v))continue;
   auto type=obs_property_get_type(v);
   out<<QString::fromUtf8(obs_property_name(v))<<QString::fromUtf8(obs_property_description(v))
      <<qint32(type)<<obs_property_enabled(v);
   switch(type){
   case OBS_PROPERTY_GROUP:appendShape(out,obs_property_group_content(v));break;
   case OBS_PROPERTY_FLOAT:out<<obs_property_float_min(v)<<obs_property_float_max(v)<<obs_property_float_step(v)<<qint32(obs_property_float_type(v));break;
   case OBS_PROPERTY_INT:out<<obs_property_int_min(v)<<obs_property_int_max(v)<<obs_property_int_step(v)<<qint32(obs_property_int_type(v));break;
   case OBS_PROPERTY_TEXT:out<<qint32(obs_property_text_type(v));break;
   case OBS_PROPERTY_PATH:out<<QString::fromUtf8(obs_property_path_filter(v));break;
   case OBS_PROPERTY_LIST:{
    auto fmt=obs_property_list_format(v);out<<qint32(fmt)<<quint64(obs_property_list_item_count(v));
    for(size_t i=0;i<obs_property_list_item_count(v);++i){
     out<<QString::fromUtf8(obs_property_list_item_name(v,i))<<obs_property_list_item_disabled(v,i);
     if(fmt==OBS_COMBO_FORMAT_INT)out<<qint64(obs_property_list_item_int(v,i));
     else if(fmt==OBS_COMBO_FORMAT_STRING)out<<QString::fromUtf8(obs_property_list_item_string(v,i));
    }break;
   }
   default:break;
   }
  }
  out<<QString(); // end of this property group
 }
 static QByteArray shapeOf(obs_properties_t*p,obs_data_t*s){
  QByteArray shape;QDataStream out(&shape,QIODevice::WriteOnly);appendShape(out,p);
  out<<string(s,"perf_mod_shader")<<string(s,"perf_mod_param")<<string(s,"perf_mod_data_uuid")
     <<qint64(obs_data_get_int(s,"perf_mod_source"))<<qint64(obs_data_get_int(s,"perf_mod_data_age_ms"))
     <<obs_data_get_double(s,"perf_mod_depth")<<obs_data_get_double(s,"perf_mod_hz");
  QStringList providers;
  obs_enum_sources([](void*data,obs_source_t*source){
   if(!obs_source_removed(source)&&!strcmp(obs_source_get_id(source),"url_source")){
    auto*list=static_cast<QStringList*>(data);
    *list<<QString::fromUtf8(obs_source_get_uuid(source))+"\n"+QString::fromUtf8(obs_source_get_name(source));
   }return true;
  },&providers);
  providers.sort();out<<providers;return shape;
 }
 static QColor currentColor(obs_data_t*s,const QByteArray&k,bool&has){
  has=obs_data_has_user_value(s,k.constData())||obs_data_has_default_value(s,k.constData());
  auto val=uint32_t(obs_data_get_int(s,k.constData()));
  return QColor(val&255,(val>>8)&255,(val>>16)&255,(val>>24)&255);
 }
 void replaceForm(obs_properties_t*p,obs_data_t*s){
  fieldSync.clear();
  int y=scroll->verticalScrollBar()->value();
  auto*body=new QWidget;auto*l=new QVBoxLayout(body);l->setContentsMargins(0,0,0,0);l->setSpacing(5);
  if(p&&s){
   auto*inputs=new QGroupBox("Requested inputs",body);auto*il=new QFormLayout(inputs);
   for(auto*v=obs_properties_first(p);v;obs_property_next(&v)){
    QString key=QString::fromUtf8(obs_property_name(v));
    if(!obs_property_visible(v))continue;
    if(key=="enable_sound"||key=="audio_source"||key=="sight_use_tracking"||key=="sight_tracking_channel"||key=="mp_controlnet_model")
     addField(il,v,s);
    else if(key.startsWith("channel")&&key.endsWith("_group"))addGroup(l,v,s);
   }
   if(il->rowCount())l->insertWidget(0,inputs);else delete inputs;
   addModulation(l,s);
   for(const char*key:{"shader_params_group","global_overrides_group"})
    if(auto*v=obs_properties_get(p,key))addGroup(l,v,s);
  }
  l->addStretch();auto*old=scroll->takeWidget();scroll->setWidget(body);if(old){for(auto*child:old->findChildren<QWidget*>())child->blockSignals(true);old->deleteLater();}scroll->verticalScrollBar()->setValue(y);
 }
 void addModulation(QVBoxLayout*l,obs_data_t*s){
  if(fields.isEmpty())return;
  auto*g=new QGroupBox("Modulation",this);auto*f=new QFormLayout(g);
  f->setContentsMargins(5,7,5,5);f->setRowWrapPolicy(QFormLayout::WrapLongRows);
  auto*param=new QComboBox(g);param->setObjectName("hsModParameter");param->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);param->setMinimumContentsLength(7);
  for(const auto&field:fields){QString label=field.key.mid(6);label.replace('_',' ');param->addItem(label,field.key);}
  auto*driver=new QComboBox(g);driver->setObjectName("hsModDriver");
  driver->addItems({"Off","Oscillator","Audio · level","Audio · bass","Audio · kick","Audio · beat","Sight · motion","Sight · pinch","Data Source · value"});
  auto*depth=new QDoubleSpinBox(g);depth->setObjectName("hsModDepth");depth->setRange(0,100);depth->setSuffix("%");depth->setValue(25);
  auto*hz=new QDoubleSpinBox(g);hz->setRange(.01,10);hz->setDecimals(2);hz->setSingleStep(.1);hz->setSuffix(" Hz");hz->setValue(.5);
  auto*provider=new QComboBox(g);provider->setObjectName("hsModDataSource");
  provider->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  provider->setMinimumContentsLength(7);
  provider->addItem("Select Data Source…","");
  obs_enum_sources([](void*data,obs_source_t*source){
   auto*list=static_cast<QComboBox*>(data);
   if(!obs_source_removed(source)&&!strcmp(obs_source_get_id(source),"url_source"))
    list->addItem(QString::fromUtf8(obs_source_get_name(source)),QString::fromUtf8(obs_source_get_uuid(source)));
   return true;
  },provider);
  const auto savedProvider=string(s,"perf_mod_data_uuid");
  int providerIndex=provider->findData(savedProvider);
  if(providerIndex<0&&!savedProvider.isEmpty()){
   provider->addItem("Saved source (unavailable)",savedProvider);providerIndex=provider->count()-1;
  }
  provider->setCurrentIndex(std::max(0,providerIndex));
  auto*age=new QSpinBox(g);age->setObjectName("hsModDataAge");age->setRange(100,60000);
  age->setSuffix(" ms");age->setSingleStep(100);auto savedAge=obs_data_get_int(s,"perf_mod_data_age_ms");
  age->setValue(savedAge>0?int(std::min(60000LL,savedAge)):5000);
  const auto path=string(s,"shader_file"),token=identity;
  bool bound=path==string(s,"perf_mod_shader")&&obs_data_get_int(s,"perf_mod_source")>0;
  if(bound){
   param->setCurrentIndex(param->findData(string(s,"perf_mod_param")));
   driver->setCurrentIndex(std::clamp(int(obs_data_get_int(s,"perf_mod_source")),0,8));
   depth->setValue(obs_data_get_double(s,"perf_mod_depth")*100);
   hz->setValue(obs_data_get_double(s,"perf_mod_hz"));
  }
  auto*apply=new QPushButton("Bind",g);apply->setObjectName("hsModBind");
  auto*clear=new QPushButton("Clear",g);clear->setObjectName("hsModClear");clear->setEnabled(bound);
  auto*row=new QWidget(g);auto*buttons=new QHBoxLayout(row);buttons->setContentsMargins(0,0,0,0);buttons->addWidget(apply);buttons->addWidget(clear);
  auto*description=new QLabel(bound?"Mapping active for this shader. Base slider stays unchanged.":"One live mapping per instance. Bind an oscillator or a real input signal.",g);description->setWordWrap(true);
  f->addRow("Parameter",param);f->addRow("Driver",driver);f->addRow("Depth",depth);f->addRow("Frequency",hz);f->addRow("Data Source",provider);f->addRow("Max age",age);f->addRow(row);f->addRow(description);
  auto visibility=[=](int i){
   auto show=[&](QWidget*w,bool visible){w->setVisible(visible);if(auto*label=f->labelForField(w))label->setVisible(visible);};
   show(hz,i==1);show(depth,i!=8);show(provider,i==8);show(age,i==8);
   apply->setEnabled(i!=8||!provider->currentData().toString().isEmpty());
   description->setText(i==8?"Select a shared numeric or boolean result in URL Source. Stale or missing data uses the saved slider.":bound?"Mapping active for this shader. Base slider stays unchanged.":"One live mapping per instance.");
  };
  visibility(driver->currentIndex());
  connect(driver,qOverload<int>(&QComboBox::currentIndexChanged),g,visibility);
  connect(provider,qOverload<int>(&QComboBox::currentIndexChanged),g,[=](int){visibility(driver->currentIndex());});
  connect(apply,&QPushButton::clicked,this,[=]{
   if(token!=identity||param->currentIndex()<0||(driver->currentIndex()==8&&provider->currentData().toString().isEmpty()))return;
   withTarget([&](obs_source_t*src,obs_data_t*cfg){
    obs_data_set_string(cfg,"perf_mod_shader",path.toUtf8().constData());
    obs_data_set_string(cfg,"perf_mod_param",param->currentData().toString().toUtf8().constData());
    obs_data_set_int(cfg,"perf_mod_source",driver->currentIndex());
    obs_data_set_double(cfg,"perf_mod_depth",depth->value()/100.);
    obs_data_set_double(cfg,"perf_mod_hz",hz->value());
    obs_data_set_string(cfg,"perf_mod_data_uuid",provider->currentData().toString().toUtf8().constData());
    obs_data_set_int(cfg,"perf_mod_data_age_ms",age->value());
    obs_source_update(src,cfg);
   });QTimer::singleShot(0,this,[this]{refresh(true);});
  });
  connect(clear,&QPushButton::clicked,this,[=]{
   if(token!=identity)return;
   withTarget([&](obs_source_t*src,obs_data_t*cfg){
    obs_data_set_int(cfg,"perf_mod_source",0);
    obs_data_set_string(cfg,"perf_mod_data_uuid","");
    obs_data_set_string(cfg,"perf_mod_shader","");obs_data_set_string(cfg,"perf_mod_param","");
    obs_source_update(src,cfg);
   });QTimer::singleShot(0,this,[this]{refresh(true);});
  });
  l->addWidget(g);
 }
 void addGroup(QVBoxLayout*l,obs_property_t*v,obs_data_t*s){
  if(!obs_property_visible(v))return;
  auto*g=new QGroupBox(QString::fromUtf8(obs_property_description(v)));auto*form=new QFormLayout(g);
  form->setContentsMargins(5,7,5,5);form->setRowWrapPolicy(QFormLayout::WrapLongRows);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  auto*children=obs_property_group_content(v);
  for(auto*p=obs_properties_first(children);p;obs_property_next(&p)){
   if(!obs_property_visible(p))continue;
   if(obs_property_get_type(p)==OBS_PROPERTY_GROUP){addGroup(l,p,s);continue;}
   addField(form,p,s);
  }
  if(form->rowCount())l->addWidget(g);else delete g;
 }
 void addField(QFormLayout*l,obs_property_t*p,obs_data_t*s){
  const QString key=QString::fromUtf8(obs_property_name(p)),label=QString::fromUtf8(obs_property_description(p)),token=identity;
  const QByteArray k=key.toUtf8();auto type=obs_property_get_type(p);QWidget*w=nullptr;
  if(type==OBS_PROPERTY_BOOL){auto*c=new QCheckBox;c->setChecked(obs_data_get_bool(s,k.constData()));connect(c,&QCheckBox::toggled,this,[=](bool v){write(key,v,token);});fieldSync.push_back([=](obs_data_t*d){QSignalBlocker b(c);c->setChecked(obs_data_get_bool(d,k.constData()));});w=c;}
  else if(type==OBS_PROPERTY_FLOAT||type==OBS_PROPERTY_INT){
   auto*row=new QWidget;auto*r=new QHBoxLayout(row);r->setContentsMargins(0,0,0,0);r->setSpacing(3);
   auto*spin=new QDoubleSpinBox;bool integer=type==OBS_PROPERTY_INT;
   double lo=integer?obs_property_int_min(p):obs_property_float_min(p),hi=integer?obs_property_int_max(p):obs_property_float_max(p);
   double step=integer?obs_property_int_step(p):obs_property_float_step(p);
   spin->setDecimals(integer?0:4);spin->setRange(lo,hi);spin->setSingleStep(step>0?step:1);spin->setKeyboardTracking(false);
   spin->setValue(integer?obs_data_get_int(s,k.constData()):obs_data_get_double(s,k.constData()));
   connect(spin,&QDoubleSpinBox::editingFinished,this,[=]{write(key,integer?QVariant::fromValue((qlonglong)spin->value()):QVariant(spin->value()),token);});
   fieldSync.push_back([=](obs_data_t*d){QSignalBlocker b(spin);spin->setValue(integer?obs_data_get_int(d,k.constData()):obs_data_get_double(d,k.constData()));});
   auto it=std::find_if(fields.begin(),fields.end(),[&](const auto&f){return f.key==key;});
   if(it!=fields.end()){
    auto*slider=new QSlider(Qt::Horizontal);slider->setRange(0,1000);slider->setValue(qRound((spin->value()-lo)/(hi-lo)*1000));r->addWidget(slider,1);
    connect(slider,&QSlider::valueChanged,spin,[=](int v){QSignalBlocker b(spin);spin->setValue(lo+(hi-lo)*v/1000.);});
    connect(slider,&QSlider::sliderReleased,this,[=]{write(key,spin->value(),token);});
    fieldSync.push_back([=](obs_data_t*){QSignalBlocker b(slider);slider->setValue(qRound((spin->value()-lo)/(hi-lo)*1000));});
    auto*lock=new QCheckBox;lock->setToolTip("Protect from random variations");lock->setChecked(locks.contains(key));
    connect(lock,&QCheckBox::toggled,this,[=](bool checked){if(token!=identity)return;if(checked)locks.insert(key);else locks.remove(key);syncLooks();});
    r->addWidget(lock);
   }
   r->addWidget(spin);w=row;
  }else if(type==OBS_PROPERTY_LIST){
   auto*c=new QComboBox;c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);c->setMinimumContentsLength(7);
   auto fmt=obs_property_list_format(p);QVariant current;
   if(fmt==OBS_COMBO_FORMAT_INT)current=QVariant::fromValue((qlonglong)obs_data_get_int(s,k.constData()));
   else if(fmt==OBS_COMBO_FORMAT_STRING)current=string(s,k.constData());
   else return delete c;
   for(size_t i=0;i<obs_property_list_item_count(p);i++){
    QVariant val=fmt==OBS_COMBO_FORMAT_INT?QVariant::fromValue((qlonglong)obs_property_list_item_int(p,i)):QVariant(QString::fromUtf8(obs_property_list_item_string(p,i)));
    c->addItem(QString::fromUtf8(obs_property_list_item_name(p,i)),val);
    if(obs_property_list_item_disabled(p,i))if(auto*m=qobject_cast<QStandardItemModel*>(c->model()))m->item(int(i))->setEnabled(false);
   }
   c->setCurrentIndex(c->findData(current));connect(c,qOverload<int>(&QComboBox::activated),this,[=](int i){if(i>=0)write(key,c->itemData(i),token);});
   fieldSync.push_back([=](obs_data_t*d){QSignalBlocker b(c);QVariant value=fmt==OBS_COMBO_FORMAT_INT?QVariant::fromValue((qlonglong)obs_data_get_int(d,k.constData())):QVariant(string(d,k.constData()));c->setCurrentIndex(c->findData(value));});w=c;
  }else if(type==OBS_PROPERTY_TEXT&&obs_property_text_type(p)!=OBS_TEXT_INFO){
   auto*e=new QLineEdit(string(s,k.constData()));connect(e,&QLineEdit::editingFinished,this,[=]{write(key,e->text(),token);});fieldSync.push_back([=](obs_data_t*d){QSignalBlocker b(e);e->setText(string(d,k.constData()));});w=e;
  }else if(type==OBS_PROPERTY_PATH){
   auto*b=new QPushButton(QFileInfo(string(s,k.constData())).fileName());if(b->text().isEmpty())b->setText("Choose file");
   auto filter=QString::fromUtf8(obs_property_path_filter(p));connect(b,&QPushButton::clicked,this,[=]{auto path=QFileDialog::getOpenFileName(this,label,QString(),filter);if(!path.isEmpty())write(key,path,token);});fieldSync.push_back([=](obs_data_t*d){auto name=QFileInfo(string(d,k.constData())).fileName();b->setText(name.isEmpty()?QString("Choose file"):name);});w=b;
  }else if(type==OBS_PROPERTY_COLOR||type==OBS_PROPERTY_COLOR_ALPHA){
   auto*b=new QPushButton("Choose color");
   auto syncColor=[=](obs_data_t*d){bool has=false;auto color=currentColor(d,k,has);b->setText(has?color.name():QString("Shader default"));};
   syncColor(s);fieldSync.push_back(syncColor);
   connect(b,&QPushButton::clicked,this,[=]{
    QColor initial(Qt::white);withTarget([&](obs_source_t*,obs_data_t*d){bool has=false;auto color=currentColor(d,k,has);if(has)initial=color;});
    auto selected=QColorDialog::getColor(initial,this,label,QColorDialog::ShowAlphaChannel);if(selected.isValid()){
    quint32 rgba=quint32(selected.red())|(quint32(selected.green())<<8)|(quint32(selected.blue())<<16)|(quint32(selected.alpha())<<24);
    write(key,QVariant::fromValue((qlonglong)rgba),token);}});w=b;
  }
  if(w){w->setObjectName("hsLive_"+key);w->setEnabled(obs_property_enabled(p));l->addRow(label,w);}
 }
};
