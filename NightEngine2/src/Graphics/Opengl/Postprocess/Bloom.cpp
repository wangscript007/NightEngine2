/*!
  @file Bloom.cpp
  @author Rittikorn Tangtrongchit
  @brief Contain the Implementation of Bloom
*/
#include "Graphics/Opengl/Postprocess/Bloom.hpp"
#include "Graphics/Opengl/VertexArrayObject.hpp"

#include "Graphics/Opengl/Postprocess/PostProcessUtility.hpp"

namespace Rendering
{
  namespace Postprocess
  {
    INIT_REFLECTION_FOR(Bloom)

    static glm::ivec2 g_bloomResolutions[k_bloomPyramidCount + 1];

    //TODO: Better Bloom Falloff
    void Bloom::Init(int width, int height)
    {
      INIT_POSTPROCESSEFFECT();
      m_resolution.x = width, m_resolution.y = height;

      //FBO for 5 down scaled version
      glm::ivec2 renderSize{ m_resolution};
      for (int i = 0; i < k_bloomPyramidCount +1; ++i)
      {
        g_bloomResolutions[i] = renderSize;
        m_bloomTexture[i] = Texture::GenerateNullTexture(renderSize.x, renderSize.y
          , Texture::Channel::RGBA16F, Texture::Channel::RGBA
          , Texture::FilterMode::LINEAR, Texture::WrapMode::CLAMP_TO_EDGE);

        m_bloomFbo[i].Init();
        m_bloomFbo[i].AttachTexture(m_bloomTexture[i]);
        m_bloomFbo[i].Bind();
        m_bloomFbo[i].Unbind();

        renderSize /= 2;
      }

      //FBO Target
      {
        m_targetTexture = Texture::GenerateNullTexture(m_resolution.x, m_resolution.y
          , Texture::Channel::RGBA16F, Texture::Channel::RGBA
          , Texture::FilterMode::LINEAR, Texture::WrapMode::CLAMP_TO_EDGE);

        m_targetFbo.Init();
        m_targetFbo.AttachTexture(m_targetTexture);
        m_targetFbo.Bind();
        m_targetFbo.Unbind();
      }

      //Shaders
      m_thresholdShader.Create();
      m_thresholdShader.AttachShaderFile("Utility/fullscreenTriangle.vert");
      m_thresholdShader.AttachShaderFile("Postprocess/brightness_threshold.frag");
      m_thresholdShader.Link();

      m_blitCopyShader.Create();
      m_blitCopyShader.AttachShaderFile("Utility/fullscreenTriangle.vert");
      m_blitCopyShader.AttachShaderFile("Utility/blitCopy.frag");
      m_blitCopyShader.Link();
      
      m_bloomShader.Create();
      m_bloomShader.AttachShaderFile("Utility/fullscreenTriangle.vert");
      m_bloomShader.AttachShaderFile("Postprocess/bloom.frag");
      m_bloomShader.Link();

      //Set Uniform
      RefreshTextureUniforms();
    }

    void Bloom::Apply(VertexArrayObject& screenVAO
      , Texture& screenTexture, PostProcessUtility& ppUtility)
    {
      //Render 5 versions of downsample threshold color
      glDisable(GL_DEPTH_TEST);

      //Render the Threshold screen texture
      int offset = m_halfResolution ? 1 : 0;
      {
        int i = 0 + offset;
        //Adjust resolution scale
        glViewport(0, 0, g_bloomResolutions[i].x, g_bloomResolutions[i].y);

        //Threshold Shader for Mip0
        m_bloomFbo[i].Bind();
        {
          glClear(GL_COLOR_BUFFER_BIT);

          //Get the brightness threshold
          m_thresholdShader.Bind();
          {
            m_thresholdShader.SetUniform("u_threshold", m_bloomThreshold);
            screenTexture.BindToTextureUnit(0);

            screenVAO.Draw();
          }
          m_thresholdShader.Unbind();
        }
        m_bloomFbo[i].Unbind();
      }

      //BlitCopy Down Scaling
      for (int i = 1 + offset; i < k_bloomPyramidCount + offset; ++i)
      {
        //Adjust resolution scale
        glViewport(0, 0, g_bloomResolutions[i].x, g_bloomResolutions[i].y);

        //Threshold Shader
        m_bloomFbo[i].Bind();
        {
          glClear(GL_COLOR_BUFFER_BIT);

          //DownScaling Shader
          m_blitCopyShader.Bind();
          {
            m_bloomTexture[i - 1].BindToTextureUnit(0);
            screenVAO.Draw();
          }
          m_blitCopyShader.Unbind();
        }
        m_bloomFbo[i].Unbind();
      }

      //Blur all texture
      //Starting from the Smallest size
      glm::vec4 clearColor = glm::vec4{ 0.0f,0.0f,0.0f,1.0f };
      for (int i = k_bloomPyramidCount - 1 + offset; i >= 0 + offset; --i)
      {
        ppUtility.BlurTarget(clearColor, m_bloomFbo[i], m_bloomTexture[i], screenVAO
          , g_bloomResolutions[i], m_blurIteration, m_useKawaseBlur);
      }

      //Combine all blurred texture
      glViewport(0, 0, m_resolution.x, m_resolution.y);
      m_targetFbo.Bind();
      {
        glClear(GL_COLOR_BUFFER_BIT);
        m_bloomShader.Bind();
        {
          m_bloomShader.SetUniform("u_intensity", m_intensity);
          m_bloomShader.SetUniform("u_scattering"
            , std::clamp(m_scattering, 0.0f, 1.0f));
          //Bind all texture
          for (int i = 0; i < k_bloomPyramidCount; ++i)
          {
            m_bloomTexture[i + offset].BindToTextureUnit(i);
          }

          //Render
          screenVAO.Draw();
        }
        m_bloomShader.Unbind();
      }
      m_targetFbo.Unbind();
    }

    void Bloom::Clear(void)
    {
      m_targetFbo.Bind();
      {
        //Clear with black color
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
      }
      m_targetFbo.Unbind();
    }

    void Bloom::RefreshTextureUniforms(void)
    {
      m_thresholdShader.Bind();
      {
        m_thresholdShader.SetUniform("u_screenTexture", 0);
      }
      m_thresholdShader.Unbind();

      m_blitCopyShader.Bind();
      {
        m_blitCopyShader.SetUniform("u_screenTexture", 0);
      }
      m_blitCopyShader.Unbind();

      m_bloomShader.Bind();
      {
        m_bloomShader.SetUniform("u_screenTexture[0]", 0);
        m_bloomShader.SetUniform("u_screenTexture[1]", 1);
        m_bloomShader.SetUniform("u_screenTexture[2]", 2);
        m_bloomShader.SetUniform("u_screenTexture[3]", 3);
        m_bloomShader.SetUniform("u_screenTexture[4]", 4);
      }
      m_bloomShader.Unbind();
    }
  }
}