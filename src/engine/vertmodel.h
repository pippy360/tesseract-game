struct vertmodel : animmodel
{
    struct vert { vec pos, norm; };
    struct vvert { vec pos; half u, v; vec norm; };
    struct vvertbump : vvert { vec tangent; float bitangent; };
    struct tcvert { float u, v; };
    struct bumpvert { vec tangent; float bitangent; };
    struct tri { ushort vert[3]; };

    struct vbocacheentry
    {
        GLuint vbuf;
        animstate as;
        int millis;

        vbocacheentry() : vbuf(0) { as.cur.fr1 = as.prev.fr1 = -1; }
    };

    struct vertmesh : mesh
    {
        vert *verts;
        tcvert *tcverts;
        bumpvert *bumpverts;
        tri *tris;
        int numverts, numtris;

        int voffset, eoffset, elen;
        ushort minvert, maxvert;

        vertmesh() : verts(0), tcverts(0), bumpverts(0), tris(0)
        {
        }

        virtual ~vertmesh()
        {
            DELETEA(verts);
            DELETEA(tcverts);
            DELETEA(bumpverts);
            DELETEA(tris);
        }

        void smoothnorms(float limit = 0, bool areaweight = true)
        {
            if(((vertmeshgroup *)group)->numframes != 1)
            {
                buildnorms(areaweight);
                return;
            }
            mesh::smoothnorms(verts, numverts, tris, numtris, limit, areaweight);
        }

        void buildnorms(bool areaweight = true)
        {
            loopk(((vertmeshgroup *)group)->numframes)
                mesh::buildnorms(&verts[k*numverts], numverts, tris, numtris, areaweight);
        }

        void calctangents(bool areaweight = true)
        {
            if(bumpverts) return;
            bumpverts = new bumpvert[((vertmeshgroup *)group)->numframes*numverts];
            loopk(((vertmeshgroup *)group)->numframes)
                mesh::calctangents(&bumpverts[k*numverts], &verts[k*numverts], tcverts, numverts, tris, numtris, areaweight);
        }

        void calcbb(vec &bbmin, vec &bbmax, const matrix4x3 &m)
        {
            loopj(numverts)
            {
                vec v = m.transform(verts[j].pos);
                bbmin.min(v);
                bbmax.max(v);
            }
        }

        void genBIH(BIH::mesh &m)
        {
            m.tris = (const BIH::tri *)tris;
            m.numtris = numtris;
            m.pos = (const uchar *)&verts->pos;
            m.posstride = sizeof(vert);
            m.tc = (const uchar *)&tcverts->u;
            m.tcstride = sizeof(tcvert);
        }

        void genshadowmesh(vector<triangle> &out, const matrix4x3 &m)
        {
            loopj(numtris)
            {
                triangle &t = out.add();
                t.a = m.transform(verts[tris[j].vert[0]].pos);
                t.b = m.transform(verts[tris[j].vert[1]].pos);
                t.c = m.transform(verts[tris[j].vert[2]].pos);
            }
        }

        static inline void assignvert(vvert &vv, int j, tcvert &tc, vert &v)
        {
            vv.pos = v.pos;
            vv.norm = v.norm;
            vv.u = tc.u;
            vv.v = tc.v;
        }

        inline void assignvert(vvertbump &vv, int j, tcvert &tc, vert &v)
        {
            vv.pos = v.pos;
            vv.norm = v.norm;
            vv.u = tc.u;
            vv.v = tc.v;
            if(bumpverts)
            {
                vv.tangent = bumpverts[j].tangent;
                vv.bitangent = bumpverts[j].bitangent;
            }
        }

        template<class T>
        int genvbo(vector<ushort> &idxs, int offset, vector<T> &vverts, int *htdata, int htlen)
        {
            voffset = offset;
            eoffset = idxs.length();
            minvert = 0xFFFF;
            loopi(numtris)
            {
                tri &t = tris[i];
                loopj(3)
                {
                    int index = t.vert[j];
                    vert &v = verts[index];
                    tcvert &tc = tcverts[index];
                    T vv;
                    assignvert(vv, index, tc, v);
                    int htidx = hthash(v.pos)&(htlen-1);
                    loopk(htlen)
                    {
                        int &vidx = htdata[(htidx+k)&(htlen-1)];
                        if(vidx < 0) { vidx = idxs.add(ushort(vverts.length())); vverts.add(vv); break; }
                        else if(!memcmp(&vverts[vidx], &vv, sizeof(vv))) { minvert = min(minvert, idxs.add(ushort(vidx))); break; }
                    }
                }
            }
            minvert = min(minvert, ushort(voffset));
            maxvert = max(minvert, ushort(vverts.length()-1));
            elen = idxs.length()-eoffset;
            return vverts.length()-voffset;
        }

        int genvbo(vector<ushort> &idxs, int offset)
        {
            voffset = offset;
            eoffset = idxs.length();
            loopi(numtris)
            {
                tri &t = tris[i];
                loopj(3) idxs.add(voffset+t.vert[j]);
            }
            minvert = voffset;
            maxvert = voffset + numverts-1;
            elen = idxs.length()-eoffset;
            return numverts;
        }

        void filltc(uchar *vdata, size_t stride)
        {
            vdata = (uchar *)&((vvert *)&vdata[voffset*stride])->u;
            loopi(numverts)
            {
                ((half *)vdata)[0] = tcverts[i].u;
                ((half *)vdata)[1] = tcverts[i].v;
                vdata += stride;
            }
        }

        void interpverts(const animstate &as, bool tangents, void * RESTRICT vdata, skin &s)
        {
            const vert * RESTRICT vert1 = &verts[as.cur.fr1 * numverts],
                       * RESTRICT vert2 = &verts[as.cur.fr2 * numverts],
                       * RESTRICT pvert1 = as.interp<1 ? &verts[as.prev.fr1 * numverts] : NULL,
                       * RESTRICT pvert2 = as.interp<1 ? &verts[as.prev.fr2 * numverts] : NULL;
            #define ipvert(attrib)   v.attrib.lerp(vert1[i].attrib, vert2[i].attrib, as.cur.t)
            #define ipbvert(attrib)  v.attrib.lerp(bvert1[i].attrib, bvert2[i].attrib, as.cur.t)
            #define ipvertp(attrib)  v.attrib.lerp(pvert1[i].attrib, pvert2[i].attrib, as.prev.t).lerp(vec().lerp(vert1[i].attrib, vert2[i].attrib, as.cur.t), as.interp)
            #define ipbvertp(attrib) v.attrib.lerp(bpvert1[i].attrib, bpvert2[i].attrib, as.prev.t).lerp(vec().lerp(bvert1[i].attrib, bvert2[i].attrib, as.cur.t), as.interp)
            #define iploop(type, body) \
                loopi(numverts) \
                { \
                    type &v = ((type * RESTRICT)vdata)[i]; \
                    body; \
                }
            if(tangents)
            {
                const bumpvert * RESTRICT bvert1 = &bumpverts[as.cur.fr1 * numverts],
                               * RESTRICT bvert2 = &bumpverts[as.cur.fr2 * numverts],
                               * RESTRICT bpvert1 = as.interp<1 ? &bumpverts[as.prev.fr1 * numverts] : NULL,
                               * RESTRICT bpvert2 = as.interp<1 ? &bumpverts[as.prev.fr2 * numverts] : NULL;
                if(as.interp<1) iploop(vvertbump, { ipvertp(pos); ipvertp(norm); ipbvertp(tangent); v.bitangent = bvert1[i].bitangent; })
                else iploop(vvertbump, { ipvert(pos); ipvert(norm); ipbvert(tangent); v.bitangent = bvert1[i].bitangent; })
            }
            else
            {
                if(as.interp<1) iploop(vvert, { ipvertp(pos); ipvertp(norm); })
                else iploop(vvert, { ipvert(pos); ipvert(norm); })
            }
            #undef iploop
            #undef ipvert
            #undef ipbvert
            #undef ipvertp
            #undef ipbvertp
        }

        void render(const animstate *as, skin &s, vbocacheentry &vc)
        {
            glDrawRangeElements_(GL_TRIANGLES, minvert, maxvert, elen, GL_UNSIGNED_SHORT, &((vertmeshgroup *)group)->edata[eoffset]);
            glde++;
            xtravertsva += numverts;
        }
    };

    struct tag
    {
        char *name;
        matrix4x3 matrix;

        tag() : name(NULL) {}
        ~tag() { DELETEA(name); }
    };

    struct vertmeshgroup : meshgroup
    {
        int numframes;
        tag *tags;
        int numtags;

        static const int MAXVBOCACHE = 16;
        vbocacheentry vbocache[MAXVBOCACHE];

        ushort *edata;
        GLuint ebuf;
        bool vtangents;
        int vlen, vertsize;
        uchar *vdata;

        vertmeshgroup() : numframes(0), tags(NULL), numtags(0), edata(NULL), ebuf(0), vtangents(false), vlen(0), vertsize(0), vdata(NULL)
        {
        }

        virtual ~vertmeshgroup()
        {
            DELETEA(tags);
            if(ebuf) glDeleteBuffers_(1, &ebuf);
            loopi(MAXVBOCACHE)
            {
                if(vbocache[i].vbuf) glDeleteBuffers_(1, &vbocache[i].vbuf);
            }
            DELETEA(vdata);
        }

        int findtag(const char *name)
        {
            loopi(numtags) if(!strcmp(tags[i].name, name)) return i;
            return -1;
        }

        bool addtag(const char *name, const matrix4x3 &matrix)
        {
            int idx = findtag(name);
            if(idx >= 0)
            {
                if(!testtags) return false;
                loopi(numframes)
                {
                    tag &t = tags[i*numtags + idx];
                    t.matrix = matrix;
                }
            }
            else
            {
                tag *newtags = new tag[(numtags+1)*numframes];
                loopi(numframes)
                {
                    tag *dst = &newtags[(numtags+1)*i], *src = &tags[numtags*i];
                    if(!i)
                    {
                        loopj(numtags) swap(dst[j].name, src[j].name);
                        dst[numtags].name = newstring(name);
                    }
                    loopj(numtags) dst[j].matrix = src[j].matrix;
                    dst[numtags].matrix = matrix;
                }
                if(tags) delete[] tags;
                tags = newtags;
                numtags++;
            }
            return true;
        }

        int totalframes() const { return numframes; }

        void concattagtransform(part *p, int i, const matrix4x3 &m, matrix4x3 &n)
        {
            n.mul(m, tags[i].matrix);
        }

        void calctagmatrix(part *p, int i, const animstate &as, matrix4 &matrix)
        {
            const matrix4x3 &tag1 = tags[as.cur.fr1*numtags + i].matrix,
                            &tag2 = tags[as.cur.fr2*numtags + i].matrix;
            matrix4x3 tag;
            tag.lerp(tag1, tag2, as.cur.t);
            if(as.interp<1)
            {
                const matrix4x3 &tag1p = tags[as.prev.fr1*numtags + i].matrix,
                                &tag2p = tags[as.prev.fr2*numtags + i].matrix;
                matrix4x3 tagp;
                tagp.lerp(tag1p, tag2p, as.prev.t);
                tag.lerp(tagp, tag, as.interp);
            }
            tag.d.mul(p->model->scale * sizescale);
            matrix = matrix4(tag);
        }

        void genvbo(bool tangents, vbocacheentry &vc)
        {
            if(!vc.vbuf) glGenBuffers_(1, &vc.vbuf);
            if(ebuf) return;

            vector<ushort> idxs;

            vtangents = tangents;
            vertsize = tangents ? sizeof(vvertbump) : sizeof(vvert);
            vlen = 0;
            if(numframes>1)
            {
                looprendermeshes(vertmesh, m, vlen += m.genvbo(idxs, vlen));
                DELETEA(vdata);
                vdata = new uchar[vlen*vertsize];
                looprendermeshes(vertmesh, m, m.filltc(vdata, vertsize));
            }
            else
            {
                glBindBuffer_(GL_ARRAY_BUFFER, vc.vbuf);
                #define GENVBO(type) \
                    do \
                    { \
                        vector<type> vverts; \
                        looprendermeshes(vertmesh, m, vlen += m.genvbo(idxs, vlen, vverts, htdata, htlen)); \
                        glBufferData_(GL_ARRAY_BUFFER, vverts.length()*sizeof(type), vverts.getbuf(), GL_STATIC_DRAW); \
                    } while(0)
                int numverts = 0, htlen = 128;
                looprendermeshes(vertmesh, m, numverts += m.numverts);
                while(htlen < numverts) htlen *= 2;
                if(numverts*4 > htlen*3) htlen *= 2;
                int *htdata = new int[htlen];
                memset(htdata, -1, htlen*sizeof(int));
                if(tangents) GENVBO(vvertbump);
                else GENVBO(vvert);
                delete[] htdata;
                #undef GENVBO
                glBindBuffer_(GL_ARRAY_BUFFER, 0);
            }

            glGenBuffers_(1, &ebuf);
            glBindBuffer_(GL_ELEMENT_ARRAY_BUFFER, ebuf);
            glBufferData_(GL_ELEMENT_ARRAY_BUFFER, idxs.length()*sizeof(ushort), idxs.getbuf(), GL_STATIC_DRAW);
            glBindBuffer_(GL_ELEMENT_ARRAY_BUFFER, 0);
        }

        void bindvbo(const animstate *as, part *p, vbocacheentry &vc)
        {
            vvert *vverts = 0;
            bindpos(ebuf, vc.vbuf, &vverts->pos, vertsize);
            if(as->cur.anim&ANIM_NOSKIN)
            {
                if(enablenormals) disablenormals();
                if(enabletangents) disabletangents();

                if(p->alphatested()) bindtc(&vverts->u, vertsize);
                else if(enabletc) disabletc();
            }
            else
            {
                bindnorm(&vverts->norm, vertsize);

                if(vtangents) bindtangents(&((vvertbump *)vverts)->tangent.x, vertsize);
                else if(enabletangents) disabletangents();

                bindtc(&vverts->u, vertsize);
            }
            if(enablebones) disablebones();
        }

        void cleanup()
        {
            loopi(MAXVBOCACHE)
            {
                vbocacheentry &c = vbocache[i];
                if(c.vbuf) { glDeleteBuffers_(1, &c.vbuf); c.vbuf = 0; }
                c.as.cur.fr1 = -1;
            }
            if(ebuf) { glDeleteBuffers_(1, &ebuf); ebuf = 0; }
        }

        void preload(part *p)
        {
            if(numframes > 1) return;
            bool tangents = p->hastangents();
            if(tangents!=vtangents) cleanup();
            if(!vbocache->vbuf) genvbo(tangents, *vbocache);
        }

        void render(const animstate *as, float pitch, const vec &axis, const vec &forward, dynent *d, part *p)
        {
            if(as->cur.anim&ANIM_NORENDER)
            {
                loopv(p->links) calctagmatrix(p, p->links[i].tag, *as, p->links[i].matrix);
                return;
            }

            bool tangents = p->hastangents();
            if(tangents!=vtangents) { cleanup(); disablevbo(); }
            vbocacheentry *vc = NULL;
            if(numframes<=1) vc = vbocache;
            else
            {
                loopi(MAXVBOCACHE)
                {
                    vbocacheentry &c = vbocache[i];
                    if(!c.vbuf) continue;
                    if(c.as==*as) { vc = &c; break; }
                }
                if(!vc) loopi(MAXVBOCACHE) { vc = &vbocache[i]; if(!vc->vbuf || vc->millis < lastmillis) break; }
            }
            if(!vc->vbuf) genvbo(tangents, *vc);
            if(numframes>1)
            {
                if(vc->as!=*as)
                {
                    vc->as = *as;
                    vc->millis = lastmillis;
                    looprendermeshes(vertmesh, m,
                    {
                        m.interpverts(*as, tangents, vdata + m.voffset*vertsize, p->skins[i]);
                    });
                    glBindBuffer_(GL_ARRAY_BUFFER, vc->vbuf);
                    glBufferData_(GL_ARRAY_BUFFER, vlen*vertsize, vdata, GL_STREAM_DRAW);
                }
                vc->millis = lastmillis;
            }

            bindvbo(as, p, *vc);
            looprendermeshes(vertmesh, m,
            {
                p->skins[i].bind(m, as);
                m.render(as, p->skins[i], *vc);
            });

            loopv(p->links) calctagmatrix(p, p->links[i].tag, *as, p->links[i].matrix);
        }
    };

    vertmodel(const char *name) : animmodel(name)
    {
    }
};

template<class MDL> struct vertloader : modelloader<MDL>
{
};

template<class MDL> struct vertcommands : modelcommands<MDL, struct MDL::vertmesh>
{
    typedef struct MDL::vertmeshgroup meshgroup;
    typedef struct MDL::part part;
    typedef struct MDL::skin skin;

    static void loadpart(char *model, float *smooth)
    {
        if(!MDL::loading) { conoutf("not loading an %s", MDL::formatname()); return; }
        defformatstring(filename, "%s/%s", MDL::dir, model);
        part &mdl = MDL::loading->addpart();
        if(mdl.index) mdl.disablepitch();
        mdl.meshes = MDL::loading->sharemeshes(path(filename), double(*smooth > 0 ? cos(clamp(*smooth, 0.0f, 180.0f)*RAD) : 2));
        if(!mdl.meshes) conoutf("could not load %s", filename);
        else mdl.initskins();
    }

    static void settag(char *tagname, float *tx, float *ty, float *tz, float *rx, float *ry, float *rz)
    {
        if(!MDL::loading || MDL::loading->parts.empty()) { conoutf("not loading an %s", MDL::formatname()); return; }
        part &mdl = *(part *)MDL::loading->parts.last();
        float cx = *rx ? cosf(*rx/2*RAD) : 1, sx = *rx ? sinf(*rx/2*RAD) : 0,
              cy = *ry ? cosf(*ry/2*RAD) : 1, sy = *ry ? sinf(*ry/2*RAD) : 0,
              cz = *rz ? cosf(*rz/2*RAD) : 1, sz = *rz ? sinf(*rz/2*RAD) : 0;
        matrix4x3 m(matrix3(quat(sx*cy*cz - cx*sy*sz, cx*sy*cz + sx*cy*sz, cx*cy*sz - sx*sy*cz, cx*cy*cz + sx*sy*sz)),
                    vec(*tx, *ty, *tz));
        ((meshgroup *)mdl.meshes)->addtag(tagname, m);
    }

    static void setpitch(float *pitchscale, float *pitchoffset, float *pitchmin, float *pitchmax)
    {
        if(!MDL::loading || MDL::loading->parts.empty()) { conoutf("not loading an %s", MDL::formatname()); return; }
        part &mdl = *MDL::loading->parts.last();

        mdl.pitchscale = *pitchscale;
        mdl.pitchoffset = *pitchoffset;
        if(*pitchmin || *pitchmax)
        {
            mdl.pitchmin = *pitchmin;
            mdl.pitchmax = *pitchmax;
        }
        else
        {
            mdl.pitchmin = -360*fabs(mdl.pitchscale) + mdl.pitchoffset;
            mdl.pitchmax = 360*fabs(mdl.pitchscale) + mdl.pitchoffset;
        }
    }

    static void setanim(char *anim, int *frame, int *range, float *speed, int *priority)
    {
        if(!MDL::loading || MDL::loading->parts.empty()) { conoutf("not loading an %s", MDL::formatname()); return; }
        vector<int> anims;
        game::findanims(anim, anims);
        if(anims.empty()) conoutf("could not find animation %s", anim);
        else loopv(anims)
        {
            MDL::loading->parts.last()->setanim(0, anims[i], *frame, *range, *speed, *priority);
        }
    }

    vertcommands()
    {
        if(MDL::multiparted()) this->modelcommand(loadpart, "load", "sf");
        this->modelcommand(settag, "tag", "sffffff");
        this->modelcommand(setpitch, "pitch", "ffff");
        if(MDL::cananimate()) this->modelcommand(setanim, "anim", "siiff");
    }
};

