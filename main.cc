#include "ogl.hh"
#include "screen.hh"
#include "utils.hh"
#include "imgui/imgui.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <algorithm>
#include <stack>

enum method
{
  StackBased = 0,
  HorizontalSweep = 1,
  EarClipping = 2
};

template <typename T>
struct vec2
{
  T x, y;
};
typedef vec2<float> vertex;

struct polygon
{
  std::vector<vertex> vertices;
};
static polygon mainpoly;

struct triangule_soup
{
  std::vector<polygon> triangles;
};
static triangule_soup triangulation_result;
static bool draw_tri = false;

static int mouse_x = 0, mouse_y = 0;
static bool mouse_press = false, mouse_grab = false, mouse_right = false;
static size_t grab_idx = 0;

static shaderprogram *sp;
static GLint vattr;
static array_buffer *screenverts;
static GLint resolution_unif, time_unif, modelmat_unif, color_unif;
static shader *vs, *fs;
static vertexarray *vao;

static unsigned int ui_font_texture = 0;

void render_draw_lists(ImDrawData* draw_data) {
  ImGuiIO& io = ImGui::GetIO();
  int fb_width = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
  int fb_height = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
  if (fb_width == 0 || fb_height == 0)
    return;
  draw_data->ScaleClipRects(io.DisplayFramebufferScale);

  GLint last_texture; glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
  GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);
  GLint last_scissor_box[4]; glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box);
  glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TRANSFORM_BIT);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_SCISSOR_TEST);
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glEnableClientState(GL_COLOR_ARRAY);
  glEnable(GL_TEXTURE_2D);
  //glUseProgram(0);

  glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height);
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0.0f, io.DisplaySize.x, io.DisplaySize.y, 0.0f, -1.0f, +1.0f);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

#define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))
  for (int n = 0; n < draw_data->CmdListsCount; n++) {
    const ImDrawList* cmd_list = draw_data->CmdLists[n];
    const ImDrawVert* vtx_buffer = cmd_list->VtxBuffer.Data;
    const ImDrawIdx* idx_buffer = cmd_list->IdxBuffer.Data;
    glVertexPointer(2, GL_FLOAT, sizeof(ImDrawVert)
        , (const GLvoid*)((const char*)vtx_buffer + OFFSETOF(ImDrawVert, pos)));
    glTexCoordPointer(2, GL_FLOAT, sizeof(ImDrawVert)
        , (const GLvoid*)((const char*)vtx_buffer + OFFSETOF(ImDrawVert, uv)));
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(ImDrawVert)
        , (const GLvoid*)((const char*)vtx_buffer + OFFSETOF(ImDrawVert, col)));

    for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
      const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
      if (pcmd->UserCallback) {
        pcmd->UserCallback(cmd_list, pcmd);
      } else {
        glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->TextureId);
        glScissor((int)pcmd->ClipRect.x, (int)(fb_height - pcmd->ClipRect.w)
            , (int)(pcmd->ClipRect.z - pcmd->ClipRect.x)
            , (int)(pcmd->ClipRect.w - pcmd->ClipRect.y));
        glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount
            , sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT
            , idx_buffer);
      }
      idx_buffer += pcmd->ElemCount;
    }
  }

  glDisableClientState(GL_COLOR_ARRAY);
  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  glDisableClientState(GL_VERTEX_ARRAY);
  glBindTexture(GL_TEXTURE_2D, (GLuint)last_texture);
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glPopAttrib();
  glViewport(last_viewport[0], last_viewport[1], (GLsizei)last_viewport[2]
      , (GLsizei)last_viewport[3]);
  glScissor(last_scissor_box[0], last_scissor_box[1]
      , (GLsizei)last_scissor_box[2], (GLsizei)last_scissor_box[3]);
}

void graphics_load(screen *s) {
  glClearColor(0.06f, 0.06f, 0.06f, 1);

  const char *vsrc = _glsl(
    attribute vec2 vertex_pos;
    uniform mat4 model;
    uniform mat4 projection;
    void main() {
      gl_Position = projection * model * vec4(vertex_pos, 0.0, 1.0);
    }
  );
  const char *fsrc = _glsl(
    uniform vec2 iResolution;
    uniform float iGlobalTime;
    uniform vec3 color;
    void main() {
      gl_FragColor = vec4(color, 1.0);
    }
  );

  vs = new shader(vsrc, GL_VERTEX_SHADER);
  fs = new shader(fsrc, GL_FRAGMENT_SHADER);
  sp = new shaderprogram(*vs, *fs);

  vattr = sp->bind_attrib("vertex_pos");
  resolution_unif = sp->bind_uniform("iResolution");
  modelmat_unif = sp->bind_uniform("model");
  color_unif = sp->bind_uniform("color");

  const std::vector<float> cube_verts = {
    0, 1,
    1, 0,
    0, 0,

    0, 1,
    1, 1,
    1, 0
  };
  vao = new vertexarray;
  screenverts = new array_buffer;
  vao->bind();
  screenverts->bind();
  screenverts->upload(cube_verts);
  screenverts->unbind();
  vao->unbind();

  time_unif = sp->bind_uniform("iGlobalTime");

  sp->use_this_prog();
  glUniform2f(resolution_unif, s->window_width, s->window_height);
  glm::mat4 projection_mat = glm::ortho(0.f, (float)s->window_width
      , (float)s->window_height, 0.f, -1.f, 1.f);
  glUniformMatrix4fv(sp->bind_uniform("projection"), 1, GL_FALSE
      , glm::value_ptr(projection_mat));
  sp->dont_use_this_prog();

  ImGuiIO& io = ImGui::GetIO();
  io.RenderDrawListsFn = render_draw_lists;
  io.IniFilename = nullptr;
  io.DisplaySize = ImVec2((float)s->window_width, (float)s->window_height);

  unsigned char* pixels;
  int width, height;
  io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);

  GLint last_texture;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
  glGenTextures(1, &ui_font_texture);
  glBindTexture(GL_TEXTURE_2D, ui_font_texture );
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, width, height, 0, GL_ALPHA, GL_UNSIGNED_BYTE, pixels);

  io.Fonts->TexID = (void*)(intptr_t)ui_font_texture;

  glBindTexture(GL_TEXTURE_2D, last_texture);
}

void load(screen *s)
{
  mainpoly.vertices = {
    { 350, 150 },
    { 500, 225 },
    { 350, 300 }
  };
  graphics_load(s);
}

void key_event(char, bool) {
}

void mousemotion_event(float, float, int x, int y) {
  ImGuiIO& io = ImGui::GetIO();
  io.MousePos = ImVec2(x, y);
  mouse_x = x;
  mouse_y = y;
}

void mousebutton_event(int button, bool down) {
  ImGuiIO& io = ImGui::GetIO();
  if (button == 1) {
    mouse_press = down;
    io.MouseDown[0] = down;
  } else if (button == 2)
    io.MouseDown[1] = down;
  else if (button == 3) {
    mouse_right = down;
    io.MouseDown[2] = down;
  }
}

void triangulate(int method) {
  if (mainpoly.vertices.size() < 3)
    return;
  triangulation_result.triangles.clear();

  if (method == StackBased) {
    std::stack<vertex> vertices;
    for (const vertex &v : mainpoly.vertices)
      vertices.push(v);
    const vertex &v0 = vertices.top();
    vertices.pop();
    vertex &vh = vertices.top();
    vertices.pop();
    while (!vertices.empty()) {
      const vertex &vt = vertices.top();
      vertices.pop();
      triangulation_result.triangles.push_back({ { v0, vh, vt } });
      vh = vt;
    }
  } else if (method == HorizontalSweep) {
    std::vector<vertex> vertices = mainpoly.vertices;
    std::sort(vertices.begin(), vertices.end()
        , [](const vertex &a, const vertex &b) {
          return a.x > b.x;
        });
    std::stack<vertex> vert_stack;
    for (const vertex &v : vertices)
      vert_stack.push(v);
    vertex &v1 = vert_stack.top();
    vert_stack.pop();
    vertex &v2 = vert_stack.top();
    vert_stack.pop();
    while (!vert_stack.empty()) {
      const vertex &vt = vert_stack.top();
      vert_stack.pop();
      triangulation_result.triangles.push_back({ { v1, v2, vt } });
      v1 = v2;
      v2 = vt;
    }
  } else if (method == EarClipping) {
    std::vector<vertex> vertices = mainpoly.vertices;
    while (1) {
      bool ear_found = false;
      if (vertices.size() == 3) {
        triangulation_result.triangles.push_back({ vertices });
        break;
      }
      for (size_t curr = 0; curr < vertices.size(); curr++) {
        size_t prev, next, size = vertices.size();
        if (curr == 0) {
          prev = size - 1;
          next = 1;
        } else if (curr == size - 1) {
          prev = size - 2;
          next = 0;
        } else {
          prev = curr - 1;
          next = curr + 1;
        }
        const vertex &pv = vertices[prev], &cv = vertices[curr]
          , &nv = vertices[next];
        auto vertex_is_concave =
          [](const vertex &p, const vertex &c, const vertex &n) {
            float area_sum = 0;
            area_sum += p.x * (n.y - c.y);
            area_sum += c.x * (p.y - n.y);
            area_sum += n.x * (c.y - p.y);
            return (area_sum > 0);
          };
        if (vertex_is_concave(pv, cv, nv))
          continue;
        auto vertex_in_triangle = [](const vertex &test, const vertex &v1
            , const vertex &v2, const vertex &v3) {
          float x = test.x, y = test.y, x1 = v1.x, y1 = v1.y
            , x2 = v2.x, y2 = v2.y, x3 = v3.x, y3 = v3.y;
          float d = (x1 * (y2 - y3) + y1 * (x3 - x2) + x2 * y3 - y2 * x3)
            , t1 = (x * (y3 - y1) + y * (x1 - x3) - x1 * y3 + y1 * x3) / d
            , t2 = (x * (y2 - y1) + y * (x1 - x2) - x1 * y2 + y1 * x2) / -d
            , s = t1 + t2;
          return 0 <= t1 && t1 <= 1 && 0 <= t2 && t2 <= 1 && s <= 1;
        };
        bool has_vertices_in_triangle = false;
        for (size_t j = 0; j < size; j++) {
          if (j == prev || j == curr || j == next)
            continue;
          if (vertex_in_triangle(vertices[j], pv, cv, nv)) {
            has_vertices_in_triangle = true;
            break;
          }
        }
        if (has_vertices_in_triangle)
          continue;
        ear_found = true;
        triangulation_result.triangles.push_back({ { pv, cv, nv } });
        vertices.erase(vertices.begin() + curr);
        break;
      }
      if (!ear_found)
        break;
    }
  }
}

void update(double dt, double t, screen *s) {
  ImGuiIO& io = ImGui::GetIO();
  io.DeltaTime = dt / 1000.;

  sp->use_this_prog();
  glUniform1f(time_unif, t);
  sp->dont_use_this_prog();
}

void draw_square(glm::vec2 pos, glm::vec2 size, float rotation
    , glm::vec3 color) {
  glm::mat4 model;
  model = glm::translate(model, glm::vec3(pos, 0.f));
  model = glm::rotate(model, rotation, glm::vec3(0.f, 0.f, 1.f));
  model = glm::translate(model, glm::vec3(-0.5f * size.x, -0.5f * size.y, 0.f));
  model = glm::scale(model, glm::vec3(size, 1.0f));

  screenverts->bind();
  glVertexAttribPointer(vattr, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(vattr);
  glUniformMatrix4fv(modelmat_unif, 1, GL_FALSE, glm::value_ptr(model));
  glUniform3f(color_unif, color.x, color.y, color.z);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  screenverts->unbind();
}

void draw_line(glm::vec2 start, glm::vec2 end, glm::vec3 color) {
  const std::vector<float> line_verts = {
    start.x, start.y,
    end.x, end.y
  };
  array_buffer line_buf;
  line_buf.bind();
  line_buf.upload(line_verts);
  glVertexAttribPointer(vattr, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(vattr);

  glm::mat4 id_model;
  glUniformMatrix4fv(modelmat_unif, 1, GL_FALSE, glm::value_ptr(id_model));
  glUniform3f(color_unif, color.x, color.y, color.z);
  glDrawArrays(GL_LINES, 0, 2);
  line_buf.unbind();
}

void draw(double alpha) {
  glClear(GL_COLOR_BUFFER_BIT);

  vao->unbind();

  ImGui::NewFrame();
  const ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar
    | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
    | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse;
  const int panel_width = 400;
  ImGui::SetNextWindowSize(ImVec2(panel_width, 230));
  ImGui::SetNextWindowPos(ImVec2(1150 - panel_width, 0));
  ImGui::Begin("", (bool*)true, window_flags);
  ImGui::TextWrapped("User guide:\n\n");
  ImGui::BulletText("Left click on vertices to drag them");
  ImGui::BulletText("Right click to delete a vertex");
  ImGui::BulletText("Hover over edge midpoints (colored purple) and\nleft click "
      "to add them");
  static int method = 0;
  ImGui::Text(" ");
  ImGui::Text("Triangulation method");
  ImGui::Combo("", &method, "Stack based\0Horizontal sweep\0Ear clipping\0");
  if (method == StackBased)
    ImGui::TextWrapped("Warning: Stack based triangulation algorithm works only "
        "on convex polygons");
  else if (method == HorizontalSweep)
    ImGui::TextWrapped("Warning: Horizontal sweep algorithm is not finished: it"
        " produces wrong shapes for special cases and works only on convex"
        " polygons");
  if (ImGui::Button("Triangulate")) {
    triangulate(method);
    draw_tri = true;
  }
  ImGui::End();
  // ImGui::ShowTestWindow();
  ImGui::Render();

  vao->bind();
  sp->use_this_prog();

  auto hue_to_rgb = [](float h) {
    const float s = 1, v = 1;
    int i = h * 6;
    float f = h * 6 - i, p = v * (1 - s), q = v * (1 - f * s)
      , t = v * (1 - (1 - f) * s), r, g, b;
    switch (i % 6) {
      case 0: r = v, g = t, b = p; break;
      case 1: r = q, g = v, b = p; break;
      case 2: r = p, g = v, b = t; break;
      case 3: r = p, g = q, b = v; break;
      case 4: r = t, g = p, b = v; break;
      case 5: r = v, g = p, b = q; break;
    }
    struct { float r, g, b; } out { r, g, b };
    return out;
  };

  if (draw_tri) // draw triangulated polygon
    for (size_t i = 0; i < triangulation_result.triangles.size(); i++) {
      const polygon &triangle = triangulation_result.triangles[i];
      const vertex &v1 = triangle.vertices[0], &v2 = triangle.vertices[1]
        , &v3 = triangle.vertices[2];
      const std::vector<float> tri_verts = { v1.x, v1.y, v2.x, v2.y, v3.x, v3.y };
      array_buffer tri_buf;
      tri_buf.bind();
      tri_buf.upload(tri_verts);
      glVertexAttribPointer(vattr, 2, GL_FLOAT, GL_FALSE, 0, 0);
      glEnableVertexAttribArray(vattr);

      glm::mat4 id_model;
      glUniformMatrix4fv(modelmat_unif, 1, GL_FALSE, glm::value_ptr(id_model));
      auto color = hue_to_rgb((float)i / (float)triangulation_result.triangles.size());
      glUniform3f(color_unif, color.r, color.g, color.b);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      tri_buf.unbind();
    }

  { // draw lines
    for (size_t i = 0; i < mainpoly.vertices.size(); ++i) {
      const vertex &v = mainpoly.vertices[i];
      size_t other_idx = i + 1;
      if (i == mainpoly.vertices.size() - 1)
        other_idx = 0;
      const vertex &o = mainpoly.vertices[other_idx];
      draw_line({ v.x, v.y }, { o.x, o.y }, { 0.9, 0.9, 0.9 });
    }
  }

  { // determine clicks
    size_t nearest_idx = 0;
    auto sqdist_to_m = [](const vertex &v) {
      return (v.x - mouse_x) * (v.x - mouse_x) + (v.y - mouse_y) * (v.y - mouse_y);
    };
    float mindist = sqdist_to_m(mainpoly.vertices[nearest_idx]);
    for (size_t i = 0; i < mainpoly.vertices.size(); ++i) {
      float dist = sqdist_to_m(mainpoly.vertices[i]);
      if (dist < mindist) {
        mindist = dist;
        nearest_idx = i;
      }
    }
    if (mindist <= 24 * 24) { // grab or delete vertex
      const vertex &nearest = mainpoly.vertices[nearest_idx];
      draw_square({ nearest.x, nearest.y }, { 11, 11 }, 0, { 0.7, 0.7, 0.7 });
      if (mouse_press && !mouse_grab) {
        mouse_grab = true;
        grab_idx = nearest_idx;
        draw_tri = false;
      }
      if (mouse_right)
        if (mainpoly.vertices.size() != 2) {
          mainpoly.vertices.erase(mainpoly.vertices.begin() + nearest_idx);
          draw_tri = false;
        }
    } else { // determine mid vertex
      struct midpoint {
        size_t idx_nearest, idx_2ndnearest;
        vertex v;
        float dist;
      };
      std::vector<midpoint> mps;
      for (size_t i = 0; i < mainpoly.vertices.size(); ++i) {
        size_t other_idx = i + 1;
        if (i == mainpoly.vertices.size() - 1)
          other_idx = 0;
        const vertex &v = mainpoly.vertices[i], &o = mainpoly.vertices[other_idx]
          , middle = { (v.x + o.x) / 2.f, (v.y + o.y) / 2.f };
        if (sqdist_to_m(v) < sqdist_to_m(o))
          mps.push_back({ i, other_idx, middle, sqdist_to_m(middle) });
        else
          mps.push_back({ other_idx, i, middle, sqdist_to_m(middle) });
      }
      std::sort(mps.begin(), mps.end()
          , [](const midpoint &a, const midpoint &b) {
            return a.dist < b.dist;
          });
      vertex middle = mps[0].v;
      if (sqdist_to_m(middle) > 25 * 25)
        draw_square({ middle.x, middle.y }, { 6, 6 }, 0, { 0.3, 0.06, 0.5 });
      else {
        draw_square({ middle.x, middle.y }, { 6, 6 }, 0, { 0.6, 0.12, 1 });
        if (mouse_press && !mouse_grab) { // add new vertex
          mouse_press = false;
          size_t mp_idx_nearest = mps[0].idx_nearest
            , mp_idx_2ndnearest = mps[0].idx_2ndnearest;
          int insidx = -1;
          size_t last = mainpoly.vertices.size() - 1;
          if (mp_idx_nearest == 0) {
            if (mp_idx_2ndnearest == last)
              mainpoly.vertices.push_back(middle);
            else
              insidx = 1;
          } else if (mp_idx_nearest == last)
            if (mp_idx_2ndnearest == 0)
              mainpoly.vertices.push_back(middle);
            else
              insidx = last;
          else
            insidx = std::max(mp_idx_nearest, mp_idx_2ndnearest);
          if (insidx != -1)
            mainpoly.vertices.insert(mainpoly.vertices.begin() + insidx, middle);
        }
      }
    }
    if (mouse_grab) {
      mainpoly.vertices[grab_idx] = { (float)mouse_x, (float)mouse_y };
      if (!mouse_press)
        mouse_grab = false;
    }
  }

  // draw vertices
  for (size_t i = 0; i < mainpoly.vertices.size(); ++i) {
    const vertex &v = mainpoly.vertices[i];
    // auto color = hue_to_rgb((float)i / (float)mainpoly.vertices.size());
    // draw_square({ v.x, v.y }, { 7, 7 }, 0, { color.r, color.g, color.b });
    draw_square({ v.x, v.y }, { 7, 7 }, 0, { 0.98, 0, 0 });
  }

  sp->dont_use_this_prog();
}

void cleanup() {
  if (ui_font_texture) {
    glDeleteTextures(1, &ui_font_texture);
    ImGui::GetIO().Fonts->TexID = 0;
  }
  ImGui::Shutdown();

  delete vs;
  delete fs;
  delete sp;
  delete screenverts;
  delete vao;
}

int main() {
  try {
    screen s(1150, 730);

    s.mainloop(load, key_event, mousemotion_event, mousebutton_event, update
        , draw, cleanup);
  } catch (const std::exception &e) {
    die("exception exit: %s", e.what());
  } catch (...) {
    die("unknown exception exit");
  }

  return 0;
}

