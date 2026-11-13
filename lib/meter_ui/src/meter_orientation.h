#pragma once
#include <atomic>
#include "meter_ui.h"

namespace meter_ui {
inline bool validRotation(unsigned value){return value<4;}

// Requests cross task boundaries; the display loop owns application and NVS I/O.
// The sentinel is outside the complete 0..3 setting domain, including rotation 0.
class OrientationSetting {
    const unsigned fallback_;
    std::atomic<unsigned> current_,pending_{4};
    std::atomic<bool> saved_{false};
public:
    explicit OrientationSetting(unsigned fallback=1):fallback_(validRotation(fallback)?fallback:1),current_(fallback_){}
    void load(unsigned stored,bool saved){const bool valid=validRotation(stored);current_.store(valid?stored:fallback_);pending_.store(4);saved_.store(valid&&saved);}
    unsigned rotation()const{return current_.load();}
    bool saved()const{return saved_.load();}
    bool request(unsigned value){if(!validRotation(value))return false;pending_.store(value);return true;}
    bool consume(unsigned &out){const unsigned value=pending_.exchange(4);if(!validRotation(value)||(value==current_.load()&&saved_.load()))return false;saved_.store(false);current_.store(value);out=value;return true;}
    void markSaved(bool value){saved_.store(value);}
};

namespace orientation_detail {
// Position scaling and type size are deliberately separate. A quarter turn
// provides more vertical room; it must not turn a 16px label into an 8px label.
inline int font(int size,const Transform &tx){return size>0?std::max(size,tx.font(size)):0;}
inline Label label(Label value,const Transform &tx){const auto result=tx.label(value);return {result.x,result.y,font(value.size,tx),value.align};}
inline int length(int value,const Transform &tx){return value>0?std::max(1,int(int64_t(value)*std::min(tx.width*1024/tx.reference.reference_width,tx.height*1024/tx.reference.reference_height)/1024)):0;}
template<class T> inline void canvas(T &spec,int width,int height,unsigned rotation){spec.rotation=int(rotation);spec.reference_width=width;spec.reference_height=height;}
inline bool aspectChanged(View source,int width,int height){return (source.reference_width>source.reference_height)!=(width>height);}
inline void reading(ReadingSpec &s,OverviewSpec &o,const Transform &tx){
    s.safe_margin=length(s.safe_margin,tx);
    s.value={tx.y(s.value.baseline),tx.xLength(s.value.max_width),font(s.value.size_x,tx),font(s.value.size_y,tx)};
    s.profile=label(s.profile,tx);s.status=label(s.status,tx);s.flags=label(s.flags,tx);s.unit=label(s.unit,tx);s.slot=label(s.slot,tx);
    s.scope.plot=tx.rect(s.scope.plot);s.scope.value=label(s.scope.value,tx);s.scope.status=label(s.scope.status,tx);s.scope.mode=label(s.scope.mode,tx);s.scope.slot=label(s.scope.slot,tx);
    o.values=tx.rect(o.values);o.scope=tx.rect(o.scope);o.count_button=tx.rect(o.count_button);o.gap=length(o.gap,tx);
}
inline void setup(SetupSpec &s,const Transform &tx){
    s.safe_margin=length(s.safe_margin,tx);s.viewport=tx.rect(s.viewport);s.header=tx.rect(s.header);
    s.row={tx.x(s.row.x),tx.xLength(s.row.width),tx.yLength(s.row.height),tx.yLength(s.row.step),font(s.row.font_size,tx),s.row.columns,tx.xLength(s.row.column_gap)};
    s.info_height=tx.yLength(s.info_height);s.header_font_size=font(s.header_font_size,tx);s.footer=label(s.footer,tx);
    auto &i=s.info;i.state=label(i.state,tx);i.title=label(i.title,tx);i.address=label(i.address,tx);i.model=label(i.model,tx);i.rssi=label(i.rssi,tx);i.message=label(i.message,tx);i.message_second=label(i.message_second,tx);
    auto &n=s.network;const Rect scaled=tx.rect(n.qr);const int side=std::min(scaled.w,scaled.h);n.qr={scaled.x+(scaled.w-side)/2,scaled.y+(scaled.h-side)/2,side,side};
    n.heading=label(n.heading,tx);n.ssid=label(n.ssid,tx);n.password_caption=label(n.password_caption,tx);n.password=label(n.password,tx);n.ip_caption=label(n.ip_caption,tx);n.ip=label(n.ip,tx);n.lan=label(n.lan,tx);
}
inline void advanced(AdvancedSpec &s,const Transform &tx){
    s.safe_margin=length(s.safe_margin,tx);s.viewport=tx.rect(s.viewport);s.back=tx.rect(s.back);s.up=tx.rect(s.up);s.down=tx.rect(s.down);s.header=label(s.header,tx);s.message=label(s.message,tx);
    auto &r=s.row;r={tx.yLength(r.step),tx.yLength(r.gap),font(r.title_size,tx),font(r.detail_size,tx),font(r.info_size,tx),tx.xLength(r.padding),tx.yLength(r.title_y),tx.yLength(r.detail_y)};
    const KeyboardSpec old=s.keyboard;s.keyboard=scaledKeyboard(old,tx);s.keyboard.font_size=font(old.font_size,tx);
    auto &g=s.home_grid;g={g.columns,tx.xLength(g.column_gap),tx.yLength(g.step),tx.yLength(g.height)};
}
inline void rectangularReading(Map &m,int w,int h){
    auto &r=m.reading;const int margin=w>=300?12:6;
    r.value.max_width=w-2*margin;
    r.scope.plot={margin,58,w-2*margin,std::max(1,h-116)};
    r.scope.value={margin,12,r.scope.value.size,-1};r.scope.status={w-margin,12,r.scope.status.size,1};
    r.scope.mode={w/2,38,r.scope.mode.size,0};r.scope.slot={w/2,h-18,r.scope.slot.size,0};
    m.overview.scope={margin,50,w-2*margin,std::max(1,h-104)};
    m.overview.count_button={w-margin-108,h-48,108,44};
}
inline void rectangularSetup(SetupSpec &s,const SetupSpec &base,int w,int h){
    const int margin=w>=300?12:8,top=66;
    s.header={w-margin-std::min(116,w-2*margin),8,std::min(116,w-2*margin),44};
    s.viewport={margin,top,w-2*margin,std::max(1,h-top-24)};
    s.row={margin,w-2*margin,std::max(64,base.row.height),std::max(76,base.row.step),base.row.font_size,w>=440?2:1,w>=440?12:0};
    s.info_height=w>=440?88:150;s.header_font_size=base.header_font_size;s.footer={w/2,h-16,base.footer.size,0};
    if(w>=440){
        s.info={{w-28,10,1,1},{28,32,2,-1},{28,60,1,-1},{w-28,60,1,1},{w-28,42,0,1},{28,74,0,-1},{28,82,0,-1}};
    }else{
        s.info={{w/2,0,1,0},{w/2,28,2,0},{w/2,58,1,0},{w/2,80,1,0},{w/2,98,1,0},{margin,114,1,-1},{margin,126,1,-1}};
    }
    auto &n=s.network;
    if(w>=h){
        // Landscape uses a square QR and credentials alongside it.
        const int qr=std::max(1,std::min(h-92,w/2-16)),x=margin,y=68,cx=x+qr+(w-x-qr)/2;
        n.qr={x,y,qr,qr};n.heading={cx,y+2,base.network.heading.size,0};n.ssid={cx,y+42,base.network.ssid.size,0};
        n.password_caption={cx,y+78,base.network.password_caption.size,0};n.password={cx,y+94,base.network.password.size,0};
        n.ip_caption={cx,y+128,base.network.ip_caption.size,0};n.ip={cx,y+146,base.network.ip.size,0};n.lan={cx,h-18,base.network.lan.size,0};
        if(h<250){n.heading.size=0;n.ip_caption.size=0;n.lan.size=0;n.ssid.y=y;n.password_caption.y=y+25;n.password.y=y+38;n.ip.y=h-12;}
    }else if(w>=300){
        // Leave room for the existing 24-character AP name and wrapped password.
        const int qr=std::min(208,w-32),after=top+qr+12;
        n.qr={(w-qr)/2,top,qr,qr};n.heading={w/2,after,base.network.heading.size,0};
        n.ssid={w/2,after+44,base.network.ssid.size,0};n.password_caption={w/2,after+70,base.network.password_caption.size,0};
        n.password={w/2,after+84,base.network.password.size,0};n.ip_caption={w/2,after+122,base.network.ip_caption.size,0};
        n.ip={w/2,after+138,base.network.ip.size,0};n.lan={w/2,h-16,base.network.lan.size,0};
    }
}
inline void rectangularAdvanced(AdvancedSpec &s,const AdvancedSpec &base,int w,int h){
    const int margin=w>=300?12:6,top=56;
    s.viewport={margin,top,w-2*margin,std::max(1,h-top-24)};s.back={margin,6,std::min(76,w/3),44};
    s.header={s.back.x+s.back.w+12,12,base.header.size,-1};s.message={margin,h-16,base.message.size,-1};
    s.up={margin,h-48,80,44};s.down={w-margin-80,h-48,80,44};
    s.row=base.row;s.home_grid=base.home_grid;s.keyboard=base.keyboard;
    s.home_grid.columns=w>=440?2:1;s.home_grid.column_gap=s.home_grid.columns>1?12:0;
    if(w>=300&&h>=300){
        s.header.size=std::max(2,s.header.size);s.row.title_size=std::max(2,s.row.title_size);s.row.detail_size=std::max(2,s.row.detail_size);
        s.row.step=std::max(80,s.row.step);s.row.gap=std::max(8,s.row.gap);s.row.padding=12;s.row.title_y=10;s.row.detail_y=50;
        s.home_grid.step=std::max(104,s.home_grid.step);s.home_grid.height=std::max(92,s.home_grid.height);
        s.keyboard.minimum_key_height=std::max(44,s.keyboard.minimum_key_height);s.keyboard.control_height=std::max(44,s.keyboard.control_height);s.keyboard.footer_height=std::max(44,s.keyboard.footer_height);
        s.keyboard.preferred_columns=w>=440?8:4;s.keyboard.font_size=std::max(2,s.keyboard.font_size);
    }
}
}

// Resolve from the original configured map on each change, never from the
// previous resolved result. Invalid input retains the caller's rollback map.
inline Map orientationMap(const Map &configured,unsigned physicalWidth,unsigned physicalHeight,unsigned rotation){
    if(!physicalWidth||!physicalHeight||physicalWidth>4096||physicalHeight>4096||!validRotation(rotation))return configured;
    const View oldViews[]={view(configured.reading),view(configured.setup),view(configured.advanced)};
    for(const auto &v:oldViews)if(v.reference_width<=0||v.reference_height<=0)return configured;
    const int w=int((rotation&1)?physicalHeight:physicalWidth),h=int((rotation&1)?physicalWidth:physicalHeight);
    const Transform readingTx(oldViews[0],w,h),setupTx(oldViews[1],w,h),advancedTx(oldViews[2],w,h);
    Map result=configured;
    orientation_detail::reading(result.reading,result.overview,readingTx);
    orientation_detail::setup(result.setup,setupTx);orientation_detail::advanced(result.advanced,advancedTx);
    if(!configured.reading.safe_circle){
        if(orientation_detail::aspectChanged(oldViews[0],w,h))orientation_detail::rectangularReading(result,w,h);
        // The value screen has swipe navigation and no reserved button footer.
        if((w>=300&&h>=300)||orientation_detail::aspectChanged(oldViews[0],w,h)){
            const int inset=w>=300&&h>=300?8:6;result.overview.values={inset,inset,w-2*inset,h-2*inset};
        }
    }
    if(!configured.setup.safe_circle&&orientation_detail::aspectChanged(oldViews[1],w,h))orientation_detail::rectangularSetup(result.setup,configured.setup,w,h);
    if(!configured.advanced.safe_circle&&orientation_detail::aspectChanged(oldViews[2],w,h))orientation_detail::rectangularAdvanced(result.advanced,configured.advanced,w,h);
    orientation_detail::canvas(result.reading,w,h,rotation);orientation_detail::canvas(result.setup,w,h,rotation);orientation_detail::canvas(result.advanced,w,h,rotation);
    return result;
}
}
