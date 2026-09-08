#include "ReefScene.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QVector>

#include <cmath>

namespace graphvis {
namespace {

// One deterministic source for the whole scene. Everything that varies asks
// this, so a seed reproduces its reef exactly - which matters more than it
// sounds: a splash that draws differently on every repaint flickers, because
// setStage() repaints it several times while the application starts.
class Dice {
public:
    explicit Dice(unsigned seed):rng_(seed*2654435761u+12345u){}
    double unit(){ return rng_.generateDouble(); }
    double between(double lo,double hi){ return lo+(hi-lo)*unit(); }
    int count(int lo,int hi){ return lo+int(unit()*double(hi-lo+1))%(hi-lo+1); }
    bool chance(double probability){ return unit()<probability; }
private:
    QRandomGenerator rng_;
};

// ------------------------------------------------------------------ palettes
struct Palette {
    QColor deep, mid, shallow;     // the water, top to bottom
    double coralHue;               // where the reef's colours start
    QColor sand;
};

Palette paletteFor(Dice& dice,unsigned seed){
    // Five waters, because a reef at ten metres and a reef at forty are
    // different colours, and a lagoon is different again.
    static const Palette kWaters[5]={
        {QColor(0x05,0x1c,0x2c),QColor(0x0a,0x33,0x44),QColor(0x0f,0x22,0x38),0.03,QColor(0xd8,0xc8,0x9a)},
        {QColor(0x04,0x18,0x22),QColor(0x08,0x2c,0x39),QColor(0x0c,0x1c,0x2e),0.55,QColor(0xcf,0xc0,0x95)},
        {QColor(0x06,0x22,0x2e),QColor(0x0d,0x3e,0x45),QColor(0x11,0x2a,0x36),0.13,QColor(0xe2,0xd4,0xa8)},
        {QColor(0x03,0x14,0x24),QColor(0x07,0x26,0x3d),QColor(0x0a,0x18,0x30),0.75,QColor(0xc6,0xb8,0x90)},
        {QColor(0x07,0x24,0x24),QColor(0x0c,0x40,0x3a),QColor(0x10,0x2c,0x2e),0.28,QColor(0xdd,0xd0,0x9e)},
    };
    Palette palette=kWaters[seed%5];
    palette.coralHue=std::fmod(palette.coralHue+dice.between(-0.06,0.06)+1.0,1.0);
    return palette;
}

QColor reefColour(const Palette& palette,Dice& dice,double spread,double value,double alpha){
    const double hue=std::fmod(palette.coralHue+dice.between(-spread,spread)+1.0,1.0);
    QColor c=QColor::fromHsvF(hue,dice.between(0.45,0.8),value);
    c.setAlphaF(alpha);
    return c;
}

// ------------------------------------------------------------------- shapes
//
// Every creature is drawn from a handful of primitives at a scale, so the same
// function serves a fish in the far distance and one in the foreground.

void drawFish(QPainter& p,const QPointF& at,double length,const QColor& colour,
              bool facingRight,double tilt){
    p.save();
    p.translate(at);
    p.rotate(tilt);
    if(!facingRight) p.scale(-1.0,1.0);
    const double h=length*0.42;
    QPainterPath body;
    body.moveTo(length*0.5,0);
    body.cubicTo(length*0.2,-h*0.9,-length*0.25,-h*0.8,-length*0.42,0);
    body.cubicTo(-length*0.25,h*0.8,length*0.2,h*0.9,length*0.5,0);
    p.setPen(Qt::NoPen);
    p.setBrush(colour);
    p.drawPath(body);
    QPainterPath tail;
    tail.moveTo(-length*0.40,0);
    tail.lineTo(-length*0.62,-h*0.75);
    tail.lineTo(-length*0.54,0);
    tail.lineTo(-length*0.62,h*0.75);
    tail.closeSubpath();
    p.drawPath(tail);
    // A dorsal fin, so a fish reads as a fish at twelve pixels long.
    QPainterPath fin;
    fin.moveTo(length*0.02,-h*0.72);
    fin.lineTo(-length*0.16,-h*1.25);
    fin.lineTo(-length*0.22,-h*0.6);
    fin.closeSubpath();
    QColor finColour=colour.darker(115);
    finColour.setAlphaF(colour.alphaF());
    p.setBrush(finColour);
    p.drawPath(fin);
    if(length>9.0){
        QColor eye(0xf2,0xf6,0xf8);
        eye.setAlphaF(colour.alphaF());
        p.setBrush(eye);
        p.drawEllipse(QPointF(length*0.27,-h*0.18),length*0.055,length*0.055);
        QColor pupil(0x10,0x18,0x1c);
        pupil.setAlphaF(colour.alphaF());
        p.setBrush(pupil);
        p.drawEllipse(QPointF(length*0.29,-h*0.18),length*0.028,length*0.028);
    }
    p.restore();
}

void drawAngelfish(QPainter& p,const QPointF& at,double size,const QColor& colour,bool facingRight){
    p.save();
    p.translate(at);
    if(!facingRight) p.scale(-1.0,1.0);
    QPainterPath body;
    body.moveTo(size*0.55,0);
    body.cubicTo(size*0.1,-size*0.75,-size*0.35,-size*0.5,-size*0.45,0);
    body.cubicTo(-size*0.35,size*0.5,size*0.1,size*0.75,size*0.55,0);
    p.setPen(Qt::NoPen);
    p.setBrush(colour);
    p.drawPath(body);
    // The long trailing fins that make an angelfish an angelfish.
    QPainterPath fins;
    fins.moveTo(-size*0.05,-size*0.62);
    fins.lineTo(-size*0.55,-size*1.25);
    fins.lineTo(-size*0.3,-size*0.35);
    fins.closeSubpath();
    fins.moveTo(-size*0.05,size*0.62);
    fins.lineTo(-size*0.55,size*1.25);
    fins.lineTo(-size*0.3,size*0.35);
    fins.closeSubpath();
    QColor pale=colour.lighter(125);
    pale.setAlphaF(colour.alphaF()*0.85);
    p.setBrush(pale);
    p.drawPath(fins);
    // Stripes.
    QColor stripe=colour.darker(150);
    stripe.setAlphaF(colour.alphaF()*0.8);
    QPen pen(stripe); pen.setWidthF(size*0.09);
    p.setPen(pen);
    for(int i=-1;i<=1;++i)
        p.drawLine(QPointF(size*0.1*double(i),-size*0.55),QPointF(size*0.1*double(i)-size*0.05,size*0.55));
    p.restore();
}

void drawShoal(QPainter& p,Dice& dice,const QPointF& at,int fishCount,double length,
               const QColor& colour,bool facingRight){
    for(int i=0;i<fishCount;++i){
        const double dx=dice.between(-length*2.6,length*2.6);
        const double dy=dice.between(-length*1.5,length*1.5);
        drawFish(p,at+QPointF(dx,dy),length*dice.between(0.75,1.15),colour,facingRight,
                 dice.between(-10.0,10.0));
    }
}

void drawOctopus(QPainter& p,Dice& dice,const QPointF& at,double size,const QColor& colour){
    p.save();
    p.translate(at);
    p.setPen(Qt::NoPen);
    // Eight arms, each a tapering curve, drawn before the mantle so they leave
    // from behind it.
    for(int i=0;i<8;++i){
        const double spread=-1.0+2.0*double(i)/7.0;
        const double reach=size*dice.between(1.1,1.8);
        const double curl=dice.between(0.6,1.5)*(dice.chance(0.5)?1.0:-1.0);
        QPainterPath arm;
        const QPointF start(spread*size*0.42,size*0.28);
        arm.moveTo(start);
        const QPointF tip(spread*reach*0.9+curl*size*0.35,size*0.55+reach*0.75);
        arm.cubicTo(start+QPointF(spread*size*0.7,reach*0.35),
                    QPointF(spread*reach*0.6-curl*size*0.5,size*0.4+reach*0.55),
                    tip);
        QPen pen(colour);
        pen.setWidthF(size*0.20);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.drawPath(arm);
        // The taper: a thinner pass over the outer half.
        QPen thin(colour);
        thin.setWidthF(size*0.10);
        thin.setCapStyle(Qt::RoundCap);
        p.setPen(thin);
        QPainterPath outer;
        outer.moveTo(arm.pointAtPercent(0.55));
        outer.cubicTo(arm.pointAtPercent(0.7),arm.pointAtPercent(0.85),tip);
        p.drawPath(outer);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(colour);
    QPainterPath mantle;
    mantle.moveTo(-size*0.5,size*0.25);
    mantle.cubicTo(-size*0.62,-size*0.85,size*0.62,-size*0.85,size*0.5,size*0.25);
    mantle.cubicTo(size*0.2,size*0.5,-size*0.2,size*0.5,-size*0.5,size*0.25);
    p.drawPath(mantle);
    QColor eye(0xf6,0xf3,0xe6);
    eye.setAlphaF(colour.alphaF());
    p.setBrush(eye);
    p.drawEllipse(QPointF(-size*0.22,-size*0.12),size*0.14,size*0.11);
    p.drawEllipse(QPointF(size*0.22,-size*0.12),size*0.14,size*0.11);
    QColor pupil(0x14,0x10,0x10);
    pupil.setAlphaF(colour.alphaF());
    p.setBrush(pupil);
    p.drawEllipse(QPointF(-size*0.22,-size*0.10),size*0.06,size*0.05);
    p.drawEllipse(QPointF(size*0.22,-size*0.10),size*0.06,size*0.05);
    p.restore();
}

void drawJellyfish(QPainter& p,Dice& dice,const QPointF& at,double size,const QColor& colour){
    p.save();
    p.translate(at);
    QColor bell=colour;
    bell.setAlphaF(colour.alphaF()*0.75);
    p.setPen(Qt::NoPen);
    p.setBrush(bell);
    QPainterPath dome;
    dome.moveTo(-size,0);
    dome.cubicTo(-size,-size*1.25,size,-size*1.25,size,0);
    dome.cubicTo(size*0.5,size*0.28,-size*0.5,size*0.28,-size,0);
    p.drawPath(dome);
    QPen pen(bell);
    pen.setWidthF(size*0.11);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    for(int i=0;i<6;++i){
        const double x=-size*0.7+size*1.4*double(i)/5.0;
        QPainterPath tentacle;
        tentacle.moveTo(x,size*0.12);
        tentacle.cubicTo(x+dice.between(-size*0.5,size*0.5),size*1.0,
                         x+dice.between(-size*0.6,size*0.6),size*1.6,
                         x+dice.between(-size*0.4,size*0.4),size*2.2);
        p.drawPath(tentacle);
    }
    p.restore();
}

void drawRay(QPainter& p,const QPointF& at,double span,const QColor& colour,double tilt){
    p.save();
    p.translate(at);
    p.rotate(tilt);
    p.setPen(Qt::NoPen);
    p.setBrush(colour);
    QPainterPath wings;
    wings.moveTo(0,-span*0.16);
    wings.cubicTo(span*0.35,-span*0.42,span*0.62,span*0.02,span*0.5,span*0.16);
    wings.cubicTo(span*0.28,span*0.10,span*0.12,span*0.16,0,span*0.22);
    wings.cubicTo(-span*0.12,span*0.16,-span*0.28,span*0.10,-span*0.5,span*0.16);
    wings.cubicTo(-span*0.62,span*0.02,-span*0.35,-span*0.42,0,-span*0.16);
    p.drawPath(wings);
    QPen tail(colour);
    tail.setWidthF(span*0.035);
    tail.setCapStyle(Qt::RoundCap);
    p.setPen(tail);
    p.drawLine(QPointF(0,span*0.2),QPointF(0,span*0.55));
    p.restore();
}

void drawTurtle(QPainter& p,const QPointF& at,double size,const QColor& colour,bool facingRight){
    p.save();
    p.translate(at);
    if(!facingRight) p.scale(-1.0,1.0);
    p.setPen(Qt::NoPen);
    QColor flipper=colour.darker(125);
    flipper.setAlphaF(colour.alphaF());
    p.setBrush(flipper);
    QPainterPath front;
    front.moveTo(size*0.2,-size*0.1);
    front.cubicTo(size*0.85,-size*0.62,size*1.0,-size*0.1,size*0.5,size*0.05);
    front.closeSubpath();
    p.drawPath(front);
    QPainterPath back;
    back.moveTo(-size*0.35,size*0.05);
    back.cubicTo(-size*0.8,size*0.4,-size*0.5,size*0.5,-size*0.2,size*0.3);
    back.closeSubpath();
    p.drawPath(back);
    p.setBrush(colour);
    p.drawEllipse(QPointF(0,0),size*0.55,size*0.4);
    p.drawEllipse(QPointF(size*0.6,-size*0.16),size*0.16,size*0.13);
    // The shell's plates, which is what makes it a turtle and not a stone.
    QPen seam(colour.darker(150));
    seam.setWidthF(size*0.035);
    p.setPen(seam);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(0,0),size*0.30,size*0.22);
    for(int i=0;i<5;++i){
        const double angle=2.0*M_PI*double(i)/5.0;
        p.drawLine(QPointF(std::cos(angle)*size*0.30,std::sin(angle)*size*0.22),
                   QPointF(std::cos(angle)*size*0.55,std::sin(angle)*size*0.40));
    }
    p.restore();
}

void drawBranchingCoral(QPainter& p,Dice& dice,const QPointF& base,double height,
                        const QColor& colour){
    // Staghorn coral, not a tree. The difference is proportion: short thick
    // segments, three or four branches at every fork rather than two long thin
    // ones, and a stem as wide as the segments are long. The first version
    // recursed into a bare winter tree, which is exactly what long thin limbs
    // and two children each produce.
    struct Branch { QPointF from; double angle,length,width; int depth; };
    QVector<Branch> queue{{base,-90.0+dice.between(-6.0,6.0),height*0.42,height*0.30,0}};
    QVector<QPointF> tips;
    p.save();
    p.setBrush(Qt::NoBrush);
    while(!queue.isEmpty()){
        const Branch b=queue.takeLast();
        const double radians=b.angle*M_PI/180.0;
        const QPointF to=b.from+QPointF(std::cos(radians)*b.length,std::sin(radians)*b.length);
        QPen pen(colour);
        pen.setWidthF(qMax(1.6,b.width));
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.drawLine(b.from,to);
        if(b.depth>=3||b.length<4.0){ tips.append(to); continue; }
        const int children=dice.count(3,4);
        for(int i=0;i<children;++i)
            queue.append({to,b.angle+dice.between(-52.0,52.0),
                          b.length*dice.between(0.62,0.82),b.width*0.55,b.depth+1});
    }
    // Polyps at the growing tips, which is where a live coral is brightest.
    p.setPen(Qt::NoPen);
    QColor polyp=colour.lighter(150);
    polyp.setAlphaF(colour.alphaF());
    p.setBrush(polyp);
    for(const QPointF& tip:tips) p.drawEllipse(tip,height*0.035,height*0.035);
    p.restore();
}

// A boulder coral: the mass a reef sits on. Without a few of these the floor is
// a line with ornaments standing on it.
void drawBoulderCoral(QPainter& p,Dice& dice,const QPointF& base,double size,
                      const QColor& colour){
    p.save();
    p.translate(base);
    p.setPen(Qt::NoPen);
    const int lobes=dice.count(2,4);
    for(int i=0;i<lobes;++i){
        const double x=dice.between(-size*0.5,size*0.5);
        const double r=size*dice.between(0.45,0.85);
        QColor lobe=colour;
        lobe.setAlphaF(colour.alphaF()*dice.between(0.8,1.0));
        p.setBrush(lobe);
        p.drawEllipse(QPointF(x,-r*0.35),r,r*0.62);
    }
    // A few pits, so the mass has a surface.
    QColor pit=colour.darker(140);
    pit.setAlphaF(colour.alphaF()*0.7);
    p.setBrush(pit);
    for(int i=0;i<dice.count(3,7);++i)
        p.drawEllipse(QPointF(dice.between(-size*0.6,size*0.6),dice.between(-size*0.7,-size*0.1)),
                      size*0.09,size*0.06);
    p.restore();
}

void drawFanCoral(QPainter& p,Dice& dice,const QPointF& base,double size,const QColor& colour){
    p.save();
    p.translate(base);
    p.rotate(dice.between(-14.0,14.0));
    QColor fill=colour;
    fill.setAlphaF(colour.alphaF()*0.55);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    QPainterPath fan;
    fan.moveTo(0,0);
    fan.cubicTo(-size*0.85,-size*0.35,-size*0.7,-size*1.05,0,-size*1.15);
    fan.cubicTo(size*0.7,-size*1.05,size*0.85,-size*0.35,0,0);
    p.drawPath(fan);
    // Ribs, which is what tells a sea fan from a leaf.
    QPen rib(colour);
    rib.setWidthF(qMax(0.8,size*0.035));
    p.setPen(rib);
    for(int i=0;i<9;++i){
        const double angle=(-160.0+20.0*double(i))*M_PI/180.0;
        p.drawLine(QPointF(0,0),QPointF(std::cos(angle)*size*0.78,std::sin(angle)*size*0.95));
    }
    p.restore();
}

// No Dice: a brain coral's grooves are regular, and the shape reads as coral
// because of that regularity rather than in spite of it.
void drawBrainCoral(QPainter& p,const QPointF& base,double size,const QColor& colour){
    p.save();
    p.translate(base);
    p.setPen(Qt::NoPen);
    p.setBrush(colour);
    QPainterPath dome;
    dome.moveTo(-size,0);
    dome.cubicTo(-size*0.95,-size*1.05,size*0.95,-size*1.05,size,0);
    dome.closeSubpath();
    p.drawPath(dome);
    QPen groove(colour.darker(145));
    groove.setWidthF(qMax(0.9,size*0.07));
    p.setPen(groove);
    p.setBrush(Qt::NoBrush);
    for(int i=0;i<4;++i){
        QPainterPath wiggle;
        const double y=-size*0.15-size*0.18*double(i);
        wiggle.moveTo(-size*0.8,y);
        for(int k=1;k<=4;++k)
            wiggle.quadTo(-size*0.8+size*0.4*double(k)-size*0.2,
                          y+((k%2)?size*0.12:-size*0.12),
                          -size*0.8+size*0.4*double(k),y);
        p.drawPath(wiggle);
    }
    p.restore();
}

void drawTubeSponge(QPainter& p,Dice& dice,const QPointF& base,double height,const QColor& colour){
    p.save();
    p.setPen(Qt::NoPen);
    const int tubes=dice.count(3,5);
    for(int i=0;i<tubes;++i){
        const double h=height*dice.between(0.55,1.0);
        const double w=h*dice.between(0.16,0.24);
        const double x=base.x()+dice.between(-height*0.35,height*0.35);
        QColor body=colour;
        body.setAlphaF(colour.alphaF());
        p.setBrush(body);
        QPainterPath tube;
        tube.moveTo(x-w,base.y());
        tube.cubicTo(x-w*0.8,base.y()-h*0.6,x-w*0.9,base.y()-h,x-w*0.7,base.y()-h);
        tube.lineTo(x+w*0.7,base.y()-h);
        tube.cubicTo(x+w*0.9,base.y()-h,x+w*0.8,base.y()-h*0.6,x+w,base.y());
        tube.closeSubpath();
        p.drawPath(tube);
        // The opening, which is the whole character of a tube sponge.
        p.setBrush(colour.darker(160));
        p.drawEllipse(QPointF(x,base.y()-h),w*0.7,w*0.28);
    }
    p.restore();
}

void drawAnemone(QPainter& p,Dice& dice,const QPointF& base,double size,const QColor& colour){
    p.save();
    p.translate(base);
    QPen tentacle(colour);
    tentacle.setWidthF(qMax(1.0,size*0.14));
    tentacle.setCapStyle(Qt::RoundCap);
    p.setPen(tentacle);
    const int arms=dice.count(9,14);
    for(int i=0;i<arms;++i){
        const double angle=(-170.0+160.0*double(i)/double(arms-1))*M_PI/180.0;
        const double reach=size*dice.between(0.7,1.15);
        QPainterPath arm;
        arm.moveTo(0,0);
        arm.quadTo(std::cos(angle)*reach*0.6,std::sin(angle)*reach*0.6+size*0.25,
                   std::cos(angle)*reach,std::sin(angle)*reach);
        p.drawPath(arm);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(colour.darker(130));
    p.drawEllipse(QPointF(0,size*0.05),size*0.42,size*0.24);
    p.restore();
}

void drawKelp(QPainter& p,Dice& dice,const QPointF& base,double height,const QColor& colour){
    p.save();
    QPen stem(colour);
    stem.setWidthF(qMax(1.2,height*0.035));
    stem.setCapStyle(Qt::RoundCap);
    p.setPen(stem);
    const double sway=dice.between(-0.35,0.35);
    QPainterPath blade;
    blade.moveTo(base);
    blade.cubicTo(base+QPointF(sway*height*0.5,-height*0.35),
                  base+QPointF(-sway*height*0.4,-height*0.7),
                  base+QPointF(sway*height*0.7,-height));
    p.drawPath(blade);
    // Leaves along the stem.
    p.setPen(Qt::NoPen);
    QColor leaf=colour;
    leaf.setAlphaF(colour.alphaF()*0.85);
    p.setBrush(leaf);
    for(double t=0.2;t<0.95;t+=0.18){
        const QPointF at=blade.pointAtPercent(t);
        const double side=(std::fmod(t*10.0,2.0)<1.0)?1.0:-1.0;
        QPainterPath frond;
        frond.moveTo(at);
        frond.quadTo(at+QPointF(side*height*0.16,-height*0.02),
                     at+QPointF(side*height*0.09,-height*0.10));
        frond.quadTo(at+QPointF(side*height*0.02,-height*0.05),at);
        p.drawPath(frond);
    }
    p.restore();
}

void drawStarfish(QPainter& p,const QPointF& at,double size,const QColor& colour,double tilt){
    p.save();
    p.translate(at);
    p.rotate(tilt);
    p.setPen(Qt::NoPen);
    p.setBrush(colour);
    QPainterPath star;
    for(int i=0;i<5;++i){
        const double outer=(-90.0+72.0*double(i))*M_PI/180.0;
        const double inner=outer+36.0*M_PI/180.0;
        const QPointF tip(std::cos(outer)*size,std::sin(outer)*size*0.55);
        const QPointF waist(std::cos(inner)*size*0.42,std::sin(inner)*size*0.24);
        if(i==0) star.moveTo(tip); else star.lineTo(tip);
        star.lineTo(waist);
    }
    star.closeSubpath();
    p.drawPath(star);
    p.restore();
}

void drawUrchin(QPainter& p,Dice& dice,const QPointF& at,double size,const QColor& colour){
    p.save();
    p.translate(at);
    QPen spine(colour);
    spine.setWidthF(qMax(0.8,size*0.10));
    spine.setCapStyle(Qt::RoundCap);
    p.setPen(spine);
    const int spines=dice.count(10,16);
    for(int i=0;i<spines;++i){
        const double angle=(-180.0+180.0*double(i)/double(spines-1))*M_PI/180.0;
        const double reach=size*dice.between(1.0,1.6);
        p.drawLine(QPointF(0,0),QPointF(std::cos(angle)*reach,std::sin(angle)*reach*0.8));
    }
    p.setPen(Qt::NoPen);
    p.setBrush(colour.darker(140));
    p.drawEllipse(QPointF(0,0),size*0.5,size*0.38);
    p.restore();
}

void drawCrab(QPainter& p,const QPointF& at,double size,const QColor& colour){
    p.save();
    p.translate(at);
    QPen leg(colour);
    leg.setWidthF(qMax(1.0,size*0.11));
    leg.setCapStyle(Qt::RoundCap);
    p.setPen(leg);
    for(int side=-1;side<=1;side+=2)
        for(int i=0;i<3;++i){
            const double x=double(side)*size*(0.5+0.16*double(i));
            p.drawLine(QPointF(double(side)*size*0.35,0),
                       QPointF(x,size*(0.28+0.12*double(i))));
        }
    // Claws.
    for(int side=-1;side<=1;side+=2){
        p.drawLine(QPointF(double(side)*size*0.4,-size*0.05),
                   QPointF(double(side)*size*0.95,-size*0.42));
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        p.drawEllipse(QPointF(double(side)*size*1.02,-size*0.5),size*0.2,size*0.15);
        p.setPen(leg);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(colour);
    p.drawEllipse(QPointF(0,0),size*0.55,size*0.36);
    QColor eye(0xf2,0xf6,0xf8);
    eye.setAlphaF(colour.alphaF());
    p.setBrush(eye);
    p.drawEllipse(QPointF(-size*0.2,-size*0.3),size*0.09,size*0.09);
    p.drawEllipse(QPointF(size*0.2,-size*0.3),size*0.09,size*0.09);
    p.restore();
}

void drawBubbles(QPainter& p,Dice& dice,const QRectF& box,int howMany){
    p.save();
    p.setPen(Qt::NoPen);
    for(int i=0;i<howMany;++i){
        const double r=dice.between(1.2,4.2);
        const QPointF at(dice.between(box.left(),box.right()),
                         dice.between(box.top(),box.bottom()));
        QColor shell(0xcf,0xef,0xf6);
        shell.setAlphaF(dice.between(0.10,0.30));
        p.setBrush(shell);
        p.drawEllipse(at,r,r);
        QColor gleam(0xff,0xff,0xff);
        gleam.setAlphaF(0.35);
        p.setBrush(gleam);
        p.drawEllipse(at+QPointF(-r*0.3,-r*0.3),r*0.28,r*0.28);
    }
    p.restore();
}

void drawLightShafts(QPainter& p,Dice& dice,const QRectF& box,int howMany){
    p.save();
    p.setPen(Qt::NoPen);
    for(int i=0;i<howMany;++i){
        const double x=dice.between(box.left()-box.width()*0.1,box.right());
        const double width=dice.between(box.width()*0.05,box.width()*0.16);
        const double lean=dice.between(box.width()*0.08,box.width()*0.26);
        QPainterPath shaft;
        shaft.moveTo(x,box.top());
        shaft.lineTo(x+width,box.top());
        shaft.lineTo(x+width+lean,box.bottom());
        shaft.lineTo(x+lean*0.7,box.bottom());
        shaft.closeSubpath();
        QLinearGradient light(QPointF(x,box.top()),QPointF(x+lean,box.bottom()));
        QColor bright(0xbf,0xe8,0xf2);
        bright.setAlphaF(dice.between(0.05,0.11));
        QColor gone=bright; gone.setAlphaF(0.0);
        light.setColorAt(0.0,bright);
        light.setColorAt(1.0,gone);
        p.fillPath(shaft,light);
    }
    p.restore();
}

// The sea floor, and everything that sits on it.
double drawSeabed(QPainter& p,Dice& dice,const QRectF& box,const Palette& palette){
    const double horizon=box.bottom()-box.height()*dice.between(0.16,0.26);
    QPainterPath floor;
    floor.moveTo(box.left(),box.bottom());
    floor.lineTo(box.left(),horizon+dice.between(-6.0,6.0));
    const int humps=dice.count(3,5);
    for(int i=1;i<=humps;++i){
        const double x=box.left()+box.width()*double(i)/double(humps);
        floor.quadTo(x-box.width()/double(humps)*0.5,
                     horizon+dice.between(-14.0,10.0),x,horizon+dice.between(-8.0,8.0));
    }
    floor.lineTo(box.right(),box.bottom());
    floor.closeSubpath();
    QLinearGradient sand(QPointF(0,horizon),QPointF(0,box.bottom()));
    // Opaque enough to be sand. At a third alpha over dark water it read as
    // grey, which is a wet pavement rather than a reef floor.
    QColor near=palette.sand; near.setAlphaF(0.82);
    QColor far=palette.sand.darker(165); far.setAlphaF(0.92);
    sand.setColorAt(0.0,near);
    sand.setColorAt(1.0,far);
    p.fillPath(floor,sand);
    // The light catching the crest of the floor, which is what gives it a
    // surface rather than a silhouette.
    QPen rim(palette.sand.lighter(135));
    rim.setWidthF(1.6);
    p.setPen(rim);
    p.setBrush(Qt::NoBrush);
    p.drawPath(floor);
    p.setPen(Qt::NoPen);
    // Grains, sparse and small.
    for(int i=0;i<40;++i){
        QColor grain=palette.sand.lighter(dice.chance(0.5)?125:80);
        grain.setAlphaF(0.5);
        p.setBrush(grain);
        const double x=dice.between(box.left(),box.right());
        const double y=dice.between(horizon+4.0,box.bottom()-2.0);
        p.drawEllipse(QPointF(x,y),dice.between(0.6,1.6),dice.between(0.4,1.0));
    }
    return horizon;
}

} // namespace

QRectF reefQuietZone(const QRectF& box){
    // The otter, the title and the progress bar. Kept as fractions so it holds
    // if the splash is ever resized.
    return QRectF(box.left()+box.width()*0.26,box.top()+box.height()*0.06,
                  box.width()*0.48,box.height()*0.82);
}

void drawReefScene(QPainter& p,const QRectF& box,unsigned seed){
    seed=seed%kReefSceneCount;
    Dice dice(seed+1u);
    const Palette palette=paletteFor(dice,seed);
    const QRectF quiet=reefQuietZone(box);

    p.save();
    p.setRenderHint(QPainter::Antialiasing,true);

    // Water.
    QLinearGradient water(box.topLeft(),box.bottomLeft());
    water.setColorAt(0.0,palette.mid);
    water.setColorAt(0.45,palette.deep);
    water.setColorAt(1.0,palette.shallow);
    p.fillRect(box,water);

    drawLightShafts(p,dice,box,dice.count(2,5));

    // A far reef, drawn dark and low-contrast, so the near one has something to
    // stand in front of.
    {
        QColor distant=palette.deep.lighter(125);
        distant.setAlphaF(0.55);
        const double base=box.bottom()-box.height()*dice.between(0.24,0.34);
        for(int i=0;i<dice.count(4,8);++i){
            const double x=dice.between(box.left(),box.right());
            if(qAbs(x-quiet.center().x())<quiet.width()*0.28&&dice.chance(0.7)) continue;
            drawBranchingCoral(p,dice,QPointF(x,base),dice.between(18.0,34.0),distant);
        }
    }

    const double horizon=drawSeabed(p,dice,box,palette);

    // The near reef, along the bottom and up the sides. Density falls off
    // towards the middle, which is where the otter is.
    const int clumps=dice.count(7,12);
    for(int i=0;i<clumps;++i){
        const double x=box.left()+box.width()*double(i)/double(qMax(1,clumps-1))
                       +dice.between(-18.0,18.0);
        const double distanceFromCentre=std::abs(x-quiet.center().x())/(box.width()*0.5);
        if(distanceFromCentre<0.45&&dice.chance(0.75)) continue;
        const double y=horizon+dice.between(2.0,box.height()*0.10);
        const double scale=0.8+0.6*distanceFromCentre;
        const int kind=dice.count(0,5);
        const QColor colour=reefColour(palette,dice,0.22,dice.between(0.55,0.9),
                                       dice.between(0.75,0.95));
        switch(kind){
        case 0: drawBranchingCoral(p,dice,QPointF(x,y),dice.between(26.0,52.0)*scale,colour); break;
        case 1: drawFanCoral(p,dice,QPointF(x,y),dice.between(16.0,30.0)*scale,colour); break;
        case 2: drawBrainCoral(p,QPointF(x,y),dice.between(11.0,20.0)*scale,colour); break;
        case 3: drawTubeSponge(p,dice,QPointF(x,y),dice.between(20.0,38.0)*scale,colour); break;
        case 4: drawAnemone(p,dice,QPointF(x,y),dice.between(11.0,19.0)*scale,colour); break;
        default: drawKelp(p,dice,QPointF(x,y),dice.between(40.0,95.0)*scale,
                          reefColour(palette,dice,0.10,0.55,0.8)); break;
        }
    }

    // Boulders and low growth right along the floor, across the whole width.
    // The clumps above thin out towards the middle so as not to crowd the
    // otter; without this the floor beneath it was bare.
    for(int i=0;i<dice.count(5,9);++i){
        const double x=dice.between(box.left()-10,box.right()+10);
        const double y=horizon+dice.between(box.height()*0.02,box.height()*0.09);
        drawBoulderCoral(p,dice,QPointF(x,y),dice.between(9.0,20.0),
                         reefColour(palette,dice,0.28,dice.between(0.45,0.75),0.9));
    }

    // Things that sit on the sand.
    for(int i=0;i<dice.count(1,4);++i){
        const double x=dice.between(box.left()+10,box.right()-10);
        const double y=horizon+dice.between(box.height()*0.04,box.height()*0.16);
        const QColor colour=reefColour(palette,dice,0.3,dice.between(0.6,0.9),0.9);
        switch(dice.count(0,2)){
        case 0: drawStarfish(p,QPointF(x,y),dice.between(7.0,13.0),colour,dice.between(0.0,360.0)); break;
        case 1: drawUrchin(p,dice,QPointF(x,y),dice.between(5.0,9.0),colour.darker(130)); break;
        default: drawCrab(p,QPointF(x,y),dice.between(6.0,10.0),colour); break;
        }
    }

    // The company the otter keeps. Every scene gets fish; the rarer animals
    // appear on their own odds, so the hundred scenes differ in cast and not
    // only in arrangement.
    const int shoals=dice.count(3,5);
    for(int i=0;i<shoals;++i){
        QPointF at(dice.between(box.left()+20,box.right()-20),
                   dice.between(box.top()+22,horizon-10));
        if(quiet.contains(at)) at.setX(dice.chance(0.5)?quiet.left()-dice.between(10,60)
                                                      :quiet.right()+dice.between(10,60));
        drawShoal(p,dice,at,dice.count(4,9),dice.between(6.0,11.0),
                  reefColour(palette,dice,0.35,dice.between(0.7,0.95),dice.between(0.6,0.9)),
                  dice.chance(0.5));
    }
    for(int i=0;i<dice.count(1,3);++i){
        QPointF at(dice.between(box.left()+30,box.right()-30),
                   dice.between(box.top()+30,horizon-16));
        if(quiet.contains(at)) at.setY(dice.between(horizon-40,horizon-12));
        drawAngelfish(p,at,dice.between(11.0,18.0),
                      reefColour(palette,dice,0.4,0.9,0.92),dice.chance(0.5));
    }
    if(dice.chance(0.45))
        drawOctopus(p,dice,QPointF(dice.chance(0.5)?box.left()+dice.between(30,70)
                                                   :box.right()-dice.between(30,70),
                                   horizon-dice.between(6.0,26.0)),
                    dice.between(14.0,22.0),reefColour(palette,dice,0.45,0.72,0.92));
    if(dice.chance(0.4))
        for(int i=0;i<dice.count(1,3);++i)
            drawJellyfish(p,dice,QPointF(dice.between(box.left()+16,box.right()-16),
                                         dice.between(box.top()+24,box.top()+box.height()*0.45)),
                          dice.between(7.0,13.0),reefColour(palette,dice,0.5,0.95,0.55));
    if(dice.chance(0.32))
        drawRay(p,QPointF(dice.between(box.left()+50,box.right()-50),
                          dice.between(box.top()+26,box.top()+box.height()*0.34)),
                dice.between(48.0,84.0),
                [&]{ QColor c=palette.deep.lighter(190); c.setAlphaF(0.62); return c; }(),
                dice.between(-12.0,12.0));
    if(dice.chance(0.28))
        drawTurtle(p,QPointF(dice.between(box.left()+40,box.right()-40),
                             dice.between(box.top()+30,horizon-40)),
                   dice.between(16.0,24.0),
                   reefColour(palette,dice,0.18,0.62,0.9),dice.chance(0.5));

    drawBubbles(p,dice,box,dice.count(10,26));

    // A little darkness at the edges, so the artwork sits in the frame rather
    // than running out of it.
    QRadialGradient vignette(box.center(),box.width()*0.8);
    vignette.setColorAt(0.62,QColor(0,0,0,0));
    vignette.setColorAt(1.0,QColor(0,0,0,70));
    p.fillRect(box,vignette);
    p.restore();
}

} // namespace graphvis
