/*
* Copyright (C) 2011 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "GLEScmContext.h"
#include "GLEScmUtils.h"
#include <GLcommon/GLutils.h>
#include <string.h>
#include <GLES/gl.h>
#include <GLES/glext.h>

void GLEScmContext::init() {
    android::Mutex::Autolock mutex(s_lock);
    if(!m_initialized) {
        s_glDispatch.dispatchFuncs(GLES_1_1);
        initCapsLocked(s_glDispatch.glGetString(GL_EXTENSIONS));
        initExtensionString();
    }
    m_texCoords = new GLESpointer[s_glSupport.maxTexUnits];
    m_map[GL_TEXTURE_COORD_ARRAY]  = &m_texCoords[m_clientActiveTexture];
    m_initialized = true;
}

GLEScmContext::GLEScmContext():GLEScontext(),m_pointsIndex(-1), m_clientActiveTexture(0) {

    m_map[GL_COLOR_ARRAY]          = new GLESpointer();
    m_map[GL_NORMAL_ARRAY]         = new GLESpointer();
    m_map[GL_VERTEX_ARRAY]         = new GLESpointer();
    m_map[GL_POINT_SIZE_ARRAY_OES] = new GLESpointer();
}


void GLEScmContext::setActiveTexture(GLenum tex) {
   m_activeTexture = tex - GL_TEXTURE0;
}

void GLEScmContext::setClientActiveTexture(GLenum tex) {
   m_clientActiveTexture = tex - GL_TEXTURE0;
   m_map[GL_TEXTURE_COORD_ARRAY] = &m_texCoords[m_clientActiveTexture];
}

GLEScmContext::~GLEScmContext(){
    m_map[GL_TEXTURE_COORD_ARRAY] = NULL;
    if(m_texCoords){
        delete[] m_texCoords;
        m_texCoords = NULL;
    }
    m_map[GL_TEXTURE_COORD_ARRAY] = NULL;
}


//setting client side arr
void GLEScmContext::setupArr(const GLvoid* arr,GLenum arrayType,GLenum dataType,GLint size,GLsizei stride,int index){
    if( arr == NULL) return;
    switch(arrayType) {
        case GL_VERTEX_ARRAY:
            s_glDispatch.glVertexPointer(size,dataType,stride,arr);
            break;
        case GL_NORMAL_ARRAY:
            s_glDispatch.glNormalPointer(dataType,stride,arr);
            break;
        case GL_TEXTURE_COORD_ARRAY:
            s_glDispatch.glTexCoordPointer(size,dataType,stride,arr);
            break;
        case GL_COLOR_ARRAY:
            s_glDispatch.glColorPointer(size,dataType,stride,arr);
            break;
        case GL_POINT_SIZE_ARRAY_OES:
            m_pointsIndex = index;
            break;
    }
}


void GLEScmContext::setupArrayPointerHelper(GLESConversionArrays& fArrs,GLint first,GLsizei count,GLenum type,const GLvoid* indices,bool direct,GLenum array_id,GLESpointer* p){
        unsigned int size = p->getSize();
        bool usingVBO = p->isVBO();
        GLenum dataType = p->getType();

        if(needConvert(fArrs,first,count,type,indices,direct,p,array_id)){
            //conversion has occured
            unsigned int convertedStride = (usingVBO && dataType != GL_BYTE) ? p->getStride() : 0;
            const void* data = (usingVBO && dataType!= GL_BYTE) ? p->getBufferData() : fArrs.getCurrentData();
            dataType = (dataType == GL_FIXED) ? GL_FLOAT:GL_SHORT;
            setupArr(data,array_id,dataType,size,convertedStride,fArrs.getCurrentIndex());
            ++fArrs;
        } else {
            const void* data = usingVBO ? p->getBufferData() : p->getArrayData();
            setupArr(data,array_id,dataType,size,p->getStride());
        }
}

void GLEScmContext::setupArraysPointers(GLESConversionArrays& fArrs,GLint first,GLsizei count,GLenum type,const GLvoid* indices,bool direct) {
    ArraysMap::iterator it;
    m_pointsIndex = -1;

    //going over all clients arrays Pointers
    for ( it=m_map.begin() ; it != m_map.end(); it++ ) {

        GLenum array_id   = (*it).first;
        GLESpointer* p = (*it).second;
        if(!isArrEnabled(array_id)) continue;
        if(array_id == GL_TEXTURE_COORD_ARRAY) continue; //handling textures later
        setupArrayPointerHelper(fArrs,first,count,type,indices,direct,array_id,p);
    }

    unsigned int activeTexture = m_clientActiveTexture + GL_TEXTURE0;

    s_lock.lock();
    int maxTexUnits = s_glSupport.maxTexUnits;
    s_lock.unlock();

    //converting all texture coords arrays
    for(int i=0; i< maxTexUnits;i++) {

        unsigned int tex = GL_TEXTURE0+i;
        setClientActiveTexture(tex);
        s_glDispatch.glClientActiveTexture(tex);

        GLenum array_id   = GL_TEXTURE_COORD_ARRAY;
        GLESpointer* p = m_map[array_id];
        if(!isArrEnabled(array_id)) continue;
        setupArrayPointerHelper(fArrs,first,count,type,indices,direct,array_id,p);
    }

    setClientActiveTexture(activeTexture);
    s_glDispatch.glClientActiveTexture(activeTexture);
}

void GLEScmContext::drawPoints(PointSizeIndices* points) {

    GLushort* indices = NULL;
    int last_size = 0;

    //drawing each group of vertices by the points size
    for(PointSizeIndices::iterator it = points->begin();it != points->end(); it++) {
            int count = (*it).second.size();
            int pointSize = (*it).first;
            std::vector<int>& arr = (*it).second;

            if(count > last_size) {
             if(indices) delete [] indices;
             indices = new GLushort[count];
            }
            int i = 0 ;
            for(std::vector<int>::iterator it2 = arr.begin();it2 != arr.end();it2++) {
                indices[i++] = (*it2);
            }
            s_glDispatch.glPointSize(pointSize);
            s_glDispatch.glDrawElements(GL_POINTS,count,GL_UNSIGNED_SHORT,indices);
    }
    if(indices) delete [] indices;
}

void  GLEScmContext::drawPointsData(GLESConversionArrays& fArrs,GLint first,GLsizei count,GLenum type,const GLvoid* indices_in,bool isElemsDraw) {
    const GLfloat  *pointsArr =  NULL;
    int stride = 0; //steps in GLfloats
    bool usingVBO = isBindedBuffer(GL_ARRAY_BUFFER);

    //choosing the right points sizes array source
    if(m_pointsIndex >= 0 && !usingVBO) { //point size array was converted
        pointsArr= (const GLfloat*)fArrs[m_pointsIndex];
        stride = 1;
    } else {
        GLESpointer* p = m_map[GL_POINT_SIZE_ARRAY_OES];
        pointsArr = static_cast<const GLfloat*>(usingVBO ? p->getBufferData():p->getArrayData());
        stride = p->getStride()?p->getStride()/sizeof(GLfloat):1;
    }

    //filling  arrays before sorting them
    PointSizeIndices  points;
    if(isElemsDraw) {
        for(int i=0; i< count; i++) {
            GLushort index = (type == GL_UNSIGNED_SHORT?
                    static_cast<const GLushort*>(indices_in)[i]:
                    static_cast<const GLubyte*>(indices_in)[i]);
            points[pointsArr[index*stride]].push_back(index);
        }
    } else {
        for(int i=0; i< count; i++) {
            points[pointsArr[first+i*stride]].push_back(i+first);
        }
    }
    drawPoints(&points);
}

void  GLEScmContext::drawPointsArrs(GLESConversionArrays& arrs,GLint first,GLsizei count) {
    drawPointsData(arrs,first,count,0,NULL,false);
}

void GLEScmContext::drawPointsElems(GLESConversionArrays& arrs,GLsizei count,GLenum type,const GLvoid* indices_in) {
    drawPointsData(arrs,0,count,type,indices_in,true);
}

bool GLEScmContext::needConvert(GLESConversionArrays& fArrs,GLint first,GLsizei count,GLenum type,const GLvoid* indices,bool direct,GLESpointer* p,GLenum array_id) {

    bool usingVBO = p->isVBO();
    GLenum arrType = p->getType();
    /*
     conversion is not necessary in the following cases:
      (*) array type is byte but it is not vertex or texture array
      (*) array type is not fixed
    */
    if((arrType != GL_FIXED) && (arrType != GL_BYTE)) return false;
    if((arrType == GL_BYTE   && (array_id != GL_VERTEX_ARRAY)) &&
       (arrType == GL_BYTE   && (array_id != GL_TEXTURE_COORD_ARRAY)) ) return false;


    bool byteVBO = (arrType == GL_BYTE) && usingVBO;
    if(byteVBO){
        p->redirectPointerData();
    }

    if(!usingVBO || byteVBO) {
        if (direct) {
            convertDirect(fArrs,first,count,array_id,p);
        } else {
            convertIndirect(fArrs,count,type,indices,array_id,p);
        }
    } else {
        if (direct) {
            convertDirectVBO(first,count,array_id,p) ;
        } else {
            convertIndirectVBO(count,type,indices,array_id,p);
        }
    }
    return true;
}

void GLEScmContext::initExtensionString() {
    *s_glExtensions = "GL_OES_blend_func_separate GL_OES_blend_equation_separate GL_OES_blend_subtract "
                      "GL_OES_byte_coordinates GL_OES_compressed_paletted_texture GL_OES_point_size_array "
                      "GL_OES_point_sprite GL_OES_single_precision GL_OES_stencil_wrap GL_OES_texture_env_crossbar "
                      "GL_OES_texture_mirored_repeat GL_OES_EGL_image GL_OES_element_index_uint GL_OES_draw_texture "
                      "GL_OES_texture_cube_map GL_OES_draw_texture ";
    if (s_glSupport.GL_OES_READ_FORMAT)
        *s_glExtensions+="GL_OES_read_format ";
    if (s_glSupport.GL_EXT_FRAMEBUFFER_OBJECT) {
        *s_glExtensions+="GL_OES_framebuffer_object GL_OES_depth24 GL_OES_depth32 GL_OES_fbo_render_mipmap "
                         "GL_OES_rgb8_rgba8 GL_OES_stencil1 GL_OES_stencil4 GL_OES_stencil8 ";
    }
    if (s_glSupport.GL_NV_PACKED_DEPTH_STENCIL)
        *s_glExtensions+="GL_OES_packed_depth_stencil ";
    if (s_glSupport.GL_EXT_TEXTURE_FORMAT_BGRA8888)
        *s_glExtensions+="GL_EXT_texture_format_BGRA8888 GL_APPLE_texture_format_BGRA8888 ";
    if (s_glSupport.GL_ARB_MATRIX_PALETTE && s_glSupport.GL_ARB_VERTEX_BLEND) {
        *s_glExtensions+="GL_OES_matrix_palette ";
        GLint max_palette_matrices=0;
        GLint max_vertex_units=0;
        dispatcher().glGetIntegerv(GL_MAX_PALETTE_MATRICES_OES,&max_palette_matrices);
        dispatcher().glGetIntegerv(GL_MAX_VERTEX_UNITS_OES,&max_vertex_units);
        if (max_palette_matrices>=32 && max_vertex_units>=4)
            *s_glExtensions+="GL_OES_extended_matrix_palette ";
    } 
}
