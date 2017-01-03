#pragma once

#include "utils.hh"
#include <GL/glew.h>
#include <vector>
#include <string>
#include <cstring>

class ogl_buffer {
protected:
  GLuint _id;
  GLenum _type;
public:
  ogl_buffer(GLenum n_type) : _type(n_type) {
    glGenBuffers(1, &_id);
  }
  ~ogl_buffer() {
    glDeleteBuffers(1, &_id);
  }
  void bind() const {
    glBindBuffer(_type, _id);
  }
  void unbind() const {
    glBindBuffer(_type, 0);
  }
};

class array_buffer : public ogl_buffer {
public:
  array_buffer() : ogl_buffer(GL_ARRAY_BUFFER) {}
  void upload(const std::vector<GLfloat> &data) {
    // bind();
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(data[0]), data.data()
        , GL_STATIC_DRAW);
    // unbind();
  }
};

static std::string get_ogl_shader_err(GLint loglen
    , void (*ogl_errmsg_func)(GLuint, GLsizei, GLsizei*, GLchar*)
    , GLuint id) {
  char msg[loglen + 1];
  ogl_errmsg_func(id, loglen, nullptr, msg);
  msg[loglen] = 0;
  std::string msgstr(msg);
  msgstr.pop_back(); // strip trailing newline
  /*
  // indent every line
  int indent = 3;
  msgstr.insert(msgstr.begin(), indent, ' ');
  for (size_t i = 0; i < msgstr.size(); i++) {
    if (msgstr[i] != '\n')
      continue;
    msgstr.insert(i, indent, ' ');
  }
  */
  return msgstr;
}

struct shader {
  GLuint type;
  GLuint id;
  shader(std::string source, GLuint ntype) : type(ntype) {
    id = glCreateShader(type);
    const char *csrc = source.c_str();
    glShaderSource(id, 1, &csrc, NULL);
    glCompileShader(id);
    GLint loglen;
    glGetShaderiv(id, GL_INFO_LOG_LENGTH, &loglen);
    if (loglen != 0) {
      std::string msg = get_ogl_shader_err(loglen, glGetShaderInfoLog, id);
      GLint compilesucc;
      glGetShaderiv(id, GL_COMPILE_STATUS, &compilesucc);
      if (compilesucc != GL_TRUE)
        die("failed to compile %s shader:\n###\n%s###"
            , type == GL_VERTEX_SHADER ? "vertex" : "fragment"
            , msg.c_str());
      else
        printf("%s shader diagnostic message:\n###\n%s###\n"
            , type == GL_VERTEX_SHADER ? "vertex" : "fragment"
            , msg.c_str());
    }
  }
  ~shader() {
    glDeleteShader(id);
  }
};

struct shaderprogram {
  GLuint id;
  shaderprogram(const shader &vert, const shader &frag) {
    assertf(vert.type == GL_VERTEX_SHADER && frag.type == GL_FRAGMENT_SHADER
          , "order of shaders in shaderprogram's constructor is reversed");
    id = glCreateProgram();
    glAttachShader(id, vert.id);
    glAttachShader(id, frag.id);
    glLinkProgram(id);
    GLint loglen;
    glGetProgramiv(id, GL_INFO_LOG_LENGTH, &loglen);
    if (loglen != 0) {
      std::string msg = get_ogl_shader_err(loglen, glGetProgramInfoLog, id);
      GLint linksucc;
      glGetProgramiv(id, GL_LINK_STATUS, &linksucc);
      if (linksucc != GL_TRUE) {
        glDetachShader(id, vert.id);
        glDetachShader(id, frag.id);
        die("failed to link a program:\n%s"
            , msg.c_str());
      } else
        printf("shader program diagnostic message:\n###\n%s###\n"
            , msg.c_str());
    }
  }
  ~shaderprogram() {
    glDeleteProgram(id);
  }
  void vertexattribptr(const array_buffer &buffer, const char *name,
      GLint size, GLenum type, GLboolean normalized, GLsizei stride,
      const GLvoid *ptr) {
    buffer.bind();
    GLint attr = glGetAttribLocation(id, name);
    glEnableVertexAttribArray(attr);
    glVertexAttribPointer(attr, size, type, normalized, stride, ptr);
    buffer.unbind();
  }
  GLint bind_attrib(const char *name) {
    GLint prev_active_prog;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_active_prog);
    use_this_prog();
    GLint attr = glGetAttribLocation(id, name);
    assertf(glGetError() == GL_NO_ERROR, "failed to bind attribute %s", name);
    glUseProgram(prev_active_prog);
    if (attr == -1)
      printf("warning: failed to bind attribute %s\n", name);
    return attr;
  }
  GLint bind_uniform(const char *name) {
    GLint prev_active_prog;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_active_prog);
    use_this_prog();
    GLint unif = glGetUniformLocation(id, name);
    assertf(glGetError() == GL_NO_ERROR, "failed to bind uniform %s", name);
    glUseProgram(prev_active_prog);
    if (0) // (unif == -1)
      printf("warning: failed to bind uniform %s\n", name);
    return unif;
  }
  void use_this_prog() {
    glUseProgram(id);
  }
  void dont_use_this_prog() {
    glUseProgram(0);
  }
};

struct vertexarray {
  GLuint id;
  vertexarray() {
    glGenVertexArrays(1, &id);
  }
  ~vertexarray() {
    glDeleteVertexArrays(1, &id);
  }
  void bind() const {
    glBindVertexArray(id);
  }
  void unbind() const {
    glBindVertexArray(0);
  }
};

