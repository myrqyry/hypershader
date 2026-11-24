/**
 * Composed shader suggestions, inspired by OpenMosh's stage/family constraints.
 * One UV-domain hero, at most one color look; always preserve sampled alpha.
 * Returns a new graph. Calling this never mutates the user's current graph.
 */
function makeCuratedGraph(catalog, random = Math.random) {
    const heroes = [
        {id:'distort_swirl', label:'Soft Swirl', values:{angle:1.25,radius:0.46}},
        {id:'distort_wave', label:'Rolling Wave', values:{frequency:11,amplitude:0.025,horizontal:true}, time:'phase'},
        {id:'distort_ripple', label:'Liquid Ripple', values:{frequency:32,amplitude:0.022},time:'phase'},
        {id:'distort_crt', label:'Subtle Curvature', values:{curvature:0.12}}
    ];
    const looks = [
        {id:'color_sepia',label:'Warm Print'},
        {id:'color_grayscale',label:'Monochrome'},
        {id:'color_invert',label:'Negative'}
    ];
    const choice = (items) => items[Math.min(items.length-1,Math.max(0,Math.floor(random()*items.length)))];
    const hero=choice(heroes);
    const look = random() < 0.75 ? choice(looks) : null;
    const graph=new ShaderGraph();
    const add=(type,x,y)=>{
        const node=graph.addNode(type,{x,y},catalog);
        if(!node)throw new Error('Missing shader node: '+type);
        return node;
    };
    const connect=(a,from,b,to)=>{
        if(!graph.connect(a.id,from,b.id,to))throw new Error('Cannot connect '+a.type+' to '+b.type);
    };
    const uv=add('input_uv',50,170);
    const deform=add(hero.id,300,150);
    Object.assign(deform.values,hero.values);
    connect(uv,'output',deform,'uv');
    if(hero.time) {
        const time=add('input_time',50,20);
        connect(time,'output',deform,hero.time);
    }
    const sample=add('tex_sample',560,150);
    connect(deform,'result',sample,'uv');
    let final=sample,finalPort='result';
    if(look) {
        const rgb=add('tex_split_rgb',790,75);
        const alpha=add('tex_split_alpha',790,290);
        const tint=add(look.id,1010,65);
        const combined=add('tex_combine',1245,160);
        connect(sample,'result',rgb,'color');
        connect(sample,'result',alpha,'color');
        connect(rgb,'result',tint,'color');
        connect(tint,'result',combined,'rgb');
        connect(alpha,'result',combined,'a');
        final=combined;
    }
    const output=add('output_color',look?1490:830,150);
    connect(final,finalPort,output,'color');
    return {graph,name:`Curated - ${hero.label}${look?' + '+look.label:''}`};
}
window.makeCuratedGraph=makeCuratedGraph;
