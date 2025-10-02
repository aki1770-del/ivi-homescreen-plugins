#include <asio/steady_timer.hpp>
#include <chrono>
#include <fstream>
#include <utility>

#include <gtest/gtest.h>

#include <flutter/encodable_value.h>

#include "flatpak/component.h"
#include "flatpak/flatpak_shim.h"

using namespace flatpak_plugin;

class FlatpakPluginTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

class ComponentTest : public ::testing::Test {
 protected:
  void SetUp() override { xmlInitParser(); }

  void TearDown() override { xmlCleanupParser(); }

  static xmlDocPtr createXmlDoc(const std::string& xmlContent) {
    return xmlParseMemory(xmlContent.c_str(),
                          static_cast<int>(xmlContent.length()));
  }

  static xmlNodePtr getRootElement(xmlDocPtr doc) {
    return xmlDocGetRootElement(doc);
  }

  static std::string componentXml() {
    return R"(
<component type="desktop">
    <id>app.authpass.AuthPass</id>
    <name>AuthPass</name>
    <summary>Password Manager: Keep your passwords safe across all platforms and devices</summary>
    <developer_name>Herbert Poul</developer_name>
    <description>
        <p>Easily and securely keep track of all your Passwords!</p>
        <p>AuthPass is a stand alone password manager with support for the popular and proven KeePass format.</p>
        <ul>
            <li>All your passwords in one place.</li>
            <li>Generate secure random passwords for each of your accounts.</li>
            <li>Keep track of your accounts across the web.</li>
        </ul>
    </description>
    <icon height="64" type="cached" width="64">app.authpass.AuthPass.png</icon>
    <icon height="128" type="cached" width="128">app.authpass.AuthPass.png</icon>
    <categories>
        <category>Security</category>
        <category>Utility</category>
    </categories>
    <kudos>
        <kudo>HiDpiIcon</kudo>
    </kudos>
    <project_license>GPL-3.0-or-later</project_license>
    <url type="homepage">https://authpass.app/</url>
    <url type="bugtracker">https://github.com/authpass/authpass/issues</url>
    <url type="donation">https://github.com/sponsors/hpoul</url>
    <url type="translate">https://translate.authpass.app</url>
    <screenshots>
        <screenshot type="default">
            <image type="source">https://data.authpass.app/data/screenshot_composition_small.png</image>
            <image height="351" type="thumbnail" width="624">https://dl.flathub.org/repo/screenshots/app.authpass.AuthPass-stable/624x351/app.authpass.AuthPass-8e73a9934daf432df01694fc5aa494e5.png</image>
        </screenshot>
    </screenshots>
    <content_rating type="oars-1.1">
        <content_attribute id="violence-cartoon">mild</content_attribute>
        <content_attribute id="language-profanity">moderate</content_attribute>
        <content_attribute id="social-info">none</content_attribute>
    </content_rating>
    <releases>
        <release timestamp="1654819200" version="1.9.6_1904">
            <description>
                <p>Bug fixes and improvements</p>
            </description>
        </release>
        <release timestamp="1650000000" version="1.9.5">
            <description>
                <p>Previous release</p>
            </description>
        </release>
    </releases>
    <launchable type="desktop-id">app.authpass.AuthPass.desktop</launchable>
    <metadata>
        <value key="flathub::build::build_log_url">https://buildbot.flathub.org/#/builders/6/builds/97962</value>
    </metadata>
    <bundle type="flatpak" runtime="org.freedesktop.Platform/x86_64/23.08" sdk="org.freedesktop.Sdk/x86_64/23.08">app/app.authpass.AuthPass/x86_64/stable</bundle>
    <keywords>
        <keyword>password</keyword>
        <keyword>security</keyword>
        <keyword>keepass</keyword>
    </keywords>
    <languages>
        <lang percentage="100">en</lang>
        <lang percentage="80">de</lang>
        <lang percentage="70">fr</lang>
    </languages>
</component>
        )";
  }
};

TEST_F(ComponentTest, Component_CompleteParsingTest) {
  std::string xmlContent = componentXml();
  xmlDocPtr doc = createXmlDoc(xmlContent);
  ASSERT_NE(doc, nullptr);

  xmlNodePtr root = getRootElement(doc);
  ASSERT_NE(root, nullptr);

  Component component(root, "en");

  EXPECT_EQ(component.getId(), "app.authpass.AuthPass");
  EXPECT_EQ(component.getName(), "AuthPass");
  EXPECT_EQ(component.getSummary(),
            "Password Manager: Keep your passwords safe across all platforms "
            "and devices");

  EXPECT_TRUE(component.getProjectLicense().has_value());
  EXPECT_EQ(component.getProjectLicense().value(), "GPL-3.0-or-later");

  EXPECT_TRUE(component.getDescription().has_value());
  EXPECT_TRUE(component.getDescription().value().find(
                  "Easily and securely keep track") != std::string::npos);

  // categories
  EXPECT_TRUE(component.getCategories().has_value());
  const auto& categories = component.getCategories().value();
  EXPECT_EQ(categories.size(), 2);
  EXPECT_TRUE(categories.find("Security") != categories.end());
  EXPECT_TRUE(categories.find("Utility") != categories.end());

  // keywords
  EXPECT_TRUE(component.getKeywords().has_value());
  const auto& keywords = component.getKeywords().value();
  EXPECT_EQ(keywords.size(), 3);
  EXPECT_TRUE(keywords.find("password") != keywords.end());
  EXPECT_TRUE(keywords.find("security") != keywords.end());
  EXPECT_TRUE(keywords.find("keepass") != keywords.end());

  // languages
  EXPECT_TRUE(component.getLanguages().has_value());
  const auto& languages = component.getLanguages().value();
  EXPECT_EQ(languages.size(), 3);
  EXPECT_TRUE(languages.find("en") != languages.end());
  EXPECT_TRUE(languages.find("de") != languages.end());
  EXPECT_TRUE(languages.find("fr") != languages.end());

  // screenshots
  EXPECT_TRUE(component.getScreenshots().has_value());
  const auto& screenshots = component.getScreenshots().value();
  EXPECT_EQ(screenshots.size(), 1);
  EXPECT_TRUE(screenshots[0].getType().has_value());
  EXPECT_EQ(screenshots[0].getType().value(), "default");

  // icons
  EXPECT_TRUE(component.getIcons().has_value());
  const auto& icons = component.getIcons().value();
  EXPECT_EQ(icons.size(), 2);

  // launchable
  EXPECT_TRUE(component.getLaunchable().has_value());
  const auto& launchable = component.getLaunchable().value();
  EXPECT_EQ(launchable.size(), 1);
  EXPECT_TRUE(launchable.find("app.authpass.AuthPass.desktop") !=
              launchable.end());

  // bundle
  EXPECT_TRUE(component.getBundle().has_value());
  EXPECT_EQ(component.getBundle().value(),
            "app/app.authpass.AuthPass/x86_64/stable");

  xmlFreeDoc(doc);
}

TEST_F(FlatpakPluginTest, GetUserInstallationsTest) {
  const auto result = FlatpakShim::GetUserInstallation();

  EXPECT_FALSE(result.value().id().empty());
  EXPECT_FALSE(result.value().display_name().empty());
}

TEST_F(FlatpakPluginTest, GetSystemInstallationsTest) {
  const auto result = FlatpakShim::GetSystemInstallations();
  auto& system_installations = result.value();
  for (auto& system_installation : system_installations) {
    EXPECT_TRUE(std::holds_alternative<flutter::CustomEncodableValue>(
        system_installation));
  }
  EXPECT_GT(result.value().size(), 0);
}

TEST_F(FlatpakPluginTest, AddRemoteTest) {
  const auto remote = Remote(
      "full-remote", "https://full.example.com/repo", "", "Full Test Remote",
      "Comprehensive test comment", "Detailed test description",
      "https://full.example.com", "https://full.example.com/icon", "22.08",
      "org.example.App", "oci", "runtime/*", "2024-01-01T00:00:00Z",
      "/var/lib/flatpak/appstream", false, true, true, false, 10);

  auto result = FlatpakShim::RemoteAdd(remote);
  ASSERT_FALSE(result.has_error());
  EXPECT_TRUE(result.value());
  if (result.value()) {
    auto cleanremote = FlatpakShim::RemoteRemove("full-remote");
  }
}

TEST_F(FlatpakPluginTest, AddEmptyRemoteTest) {
  const auto remote = Remote("", "", "", "", "", "", "", "", "", "", "", "", "",
                             "", false, false, false, false, 1);

  const auto result = FlatpakShim::RemoteAdd(remote);
  EXPECT_TRUE(result.has_error());
}

// Install a real application and uninstall it in another test to clean
// environment.
TEST_F(FlatpakPluginTest, ApplicationInstallTest) {
  if (const auto result =
          FlatpakShim::ApplicationInstall("org.gnome.Calculator");
      !result.has_error()) {
    EXPECT_EQ(result.value(), true);
  }
}

TEST_F(FlatpakPluginTest, ApplicationInstallInvalidTest) {
  const auto result = FlatpakShim::ApplicationInstall("invalid.app.test");
  EXPECT_TRUE(result.has_error());
}

TEST_F(FlatpakPluginTest, ApplicationUninstallTest) {
  const auto result = FlatpakShim::ApplicationUninstall("org.gnome.Calculator");
  EXPECT_TRUE(result.value());
}

TEST_F(FlatpakPluginTest, ApplicationUninstallInvalidTest) {
  const auto result = FlatpakShim::ApplicationUninstall("invalid.app.test");
  EXPECT_TRUE(result.has_error());
}

TEST_F(FlatpakPluginTest, GetRemoteAppsTest) {
  const auto apps = FlatpakShim::GetApplicationsRemote("flathub");

  EXPECT_GT(apps.value().size(), 0);
}

TEST_F(FlatpakPluginTest, GetApplicationsInstalledTest) {
  const auto apps = FlatpakShim::GetApplicationsInstalled();

  EXPECT_GT(apps.value().size(), 0);
}

TEST_F(FlatpakPluginTest, FindAppInRemoteSearchTest) {
  GError* error = nullptr;
  FlatpakInstallation* user_installation =
      flatpak_installation_new_user(nullptr, &error);
  auto [key, value] =
      FlatpakShim::find_app_in_remotes(user_installation, "com.spotify.Client");
  // assuming flathub remote is added
  EXPECT_FALSE(value.empty());
  EXPECT_EQ(key, "flathub");
}

TEST_F(FlatpakPluginTest, RunAppTest) {
  asio::io_context io_context;
  const asio::io_context::strand strand(io_context);
  bool tested = false;
  const auto result =
      FlatpakShim::ApplicationStart("com.spotify.Client", strand);
  ASSERT_TRUE(result.value());

  auto timer =
      std::make_shared<asio::steady_timer>(io_context, std::chrono::minutes(3));
  timer->async_wait([&tested, timer](const asio::error_code& ec) {
    if (!ec) {
      auto stop_result = FlatpakShim::ApplicationStop("com.spotify.Client");
      EXPECT_TRUE(stop_result.value());
      tested = true;
    }
  });
  io_context.run();
  EXPECT_TRUE(result.value());
}