// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "base/feature_list.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "net/base/features.h"
#include "net/test/embedded_test_server/default_handlers.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "services/cert_verifier/cert_verifier_service_factory.h"
#include "services/cert_verifier/public/mojom/cert_verifier_service_factory.mojom.h"
#include "services/network/network_context.h"
#include "services/network/network_service.h"
#include "services/network/public/cpp/features.h"
#include "services/network/public/mojom/cert_verifier_service.mojom.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/test/test_url_loader_client.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cert_verifier {
namespace {

mojo::PendingRemote<mojom::CertVerifierService> GetNewCertVerifierServiceRemote(
    mojom::CertVerifierServiceFactory* cert_verifier_service_factory,
    network::mojom::CertVerifierCreationParamsPtr creation_params) {
  mojo::PendingRemote<mojom::CertVerifierService> cert_verifier_remote;
  cert_verifier_service_factory->GetNewCertVerifier(
      cert_verifier_remote.InitWithNewPipeAndPassReceiver(),
      std::move(creation_params));
  return cert_verifier_remote;
}

}  // namespace

class NetworkContextTest : public testing::Test {
 public:
  explicit NetworkContextTest(
      base::test::TaskEnvironment::TimeSource time_source =
          base::test::TaskEnvironment::TimeSource::DEFAULT)
      : task_environment_(base::test::TaskEnvironment::MainThreadType::IO,
                          time_source),
        network_change_notifier_(
            net::NetworkChangeNotifier::CreateMockIfNeeded()),
        network_service_(network::NetworkService::CreateForTesting()) {}
  ~NetworkContextTest() override = default;

  std::unique_ptr<network::NetworkContext> CreateContextWithParams(
      network::mojom::NetworkContextParamsPtr context_params) {
    network_context_remote_.reset();
    return std::make_unique<network::NetworkContext>(
        network_service_.get(),
        network_context_remote_.BindNewPipeAndPassReceiver(),
        std::move(context_params));
  }

  network::mojom::CertVerifierParamsPtr GetCertVerifierParams(
      network::mojom::CertVerifierCreationParamsPtr
          cert_verifier_creation_params =
              network::mojom::CertVerifierCreationParams::New()) {
    if (!base::FeatureList::IsEnabled(
            network::features::kCertVerifierService)) {
      return network::mojom::CertVerifierParams::NewCreationParams(
          std::move(cert_verifier_creation_params));
    }

    if (!cert_verifier_service_factory_) {
      cert_verifier_service_factory_ =
          std::make_unique<CertVerifierServiceFactoryImpl>(
              cert_verifier_service_factory_remote_
                  .BindNewPipeAndPassReceiver());
    }

    auto cv_service_remote_params =
        network::mojom::CertVerifierServiceRemoteParams::New();

    // Create a cert verifier service.
    cv_service_remote_params->cert_verifier_service =
        GetNewCertVerifierServiceRemote(
            cert_verifier_service_factory_.get(),
            std::move(cert_verifier_creation_params));

    return network::mojom::CertVerifierParams::NewRemoteParams(
        std::move(cv_service_remote_params));
  }

  network::mojom::NetworkService* network_service() const {
    return network_service_.get();
  }

 private:
  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<net::NetworkChangeNotifier> network_change_notifier_;
  std::unique_ptr<network::NetworkService> network_service_;
  // Stores the mojo::Remote<NetworkContext> of the most recently created
  // NetworkContext. Not strictly needed, but seems best to mimic real-world
  // usage.
  mojo::Remote<network::mojom::NetworkContext> network_context_remote_;

  mojo::Remote<mojom::CertVerifierServiceFactory>
      cert_verifier_service_factory_remote_;
  std::unique_ptr<mojom::CertVerifierServiceFactory>
      cert_verifier_service_factory_;
};

#if BUILDFLAG(BUILTIN_CERT_VERIFIER_FEATURE_SUPPORTED)
namespace {

network::mojom::NetworkContextParamsPtr CreateContextParams() {
  network::mojom::NetworkContextParamsPtr params =
      network::mojom::NetworkContextParams::New();
  // Use a fixed proxy config, to avoid dependencies on local network
  // configuration.
  params->initial_proxy_config = net::ProxyConfigWithAnnotation::CreateDirect();
  return params;
}

std::unique_ptr<network::TestURLLoaderClient> FetchRequest(
    const network::ResourceRequest& request,
    network::NetworkContext* network_context,
    int url_loader_options = network::mojom::kURLLoadOptionNone,
    int process_id = network::mojom::kBrowserProcessId,
    network::mojom::URLLoaderFactoryParamsPtr params = nullptr) {
  mojo::Remote<network::mojom::URLLoaderFactory> loader_factory;
  if (!params)
    params = network::mojom::URLLoaderFactoryParams::New();
  params->process_id = process_id;
  params->is_corb_enabled = false;

  // If |site_for_cookies| is null, any non-empty NIK is fine. Otherwise, the
  // NIK must be consistent with |site_for_cookies|.
  if (request.site_for_cookies.IsNull()) {
    params->isolation_info = net::IsolationInfo::Create(
        net::IsolationInfo::RedirectMode::kUpdateNothing,
        url::Origin::Create(GURL("https://abc.invalid")),
        url::Origin::Create(GURL("https://xyz.invalid")),
        request.site_for_cookies);
  } else {
    params->isolation_info = net::IsolationInfo::CreateForInternalRequest(
        url::Origin::Create(request.site_for_cookies.RepresentativeUrl()));
  }

  network_context->CreateURLLoaderFactory(
      loader_factory.BindNewPipeAndPassReceiver(), std::move(params));

  auto client = std::make_unique<network::TestURLLoaderClient>();
  mojo::PendingRemote<network::mojom::URLLoader> loader;
  loader_factory->CreateLoaderAndStart(
      loader.InitWithNewPipeAndPassReceiver(), 0 /* routing_id */,
      0 /* request_id */, url_loader_options, request, client->CreateRemote(),
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));

  client->RunUntilComplete();
  return client;
}

}  // namespace

class UseCertVerifierBuiltinTest : public NetworkContextTest,
                                   public testing::WithParamInterface<bool> {
 public:
  UseCertVerifierBuiltinTest() = default;
  ~UseCertVerifierBuiltinTest() override = default;

  void SetUp() override {
    if (GetParam()) {
#if defined(OS_CHROMEOS)
      // TODO(crbug.com/1085379): remove this GTEST_SKIP().
      GTEST_SKIP() << "Skipping test, CertVerifierService feature not yet "
                      "available on ChromeOS.";
#else
      scoped_feature_list_.InitAndEnableFeature(
          network::features::kCertVerifierService);
#endif
    } else {
      scoped_feature_list_.InitAndDisableFeature(
          network::features::kCertVerifierService);
    }
    NetworkContextTest::SetUp();
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

TEST_P(UseCertVerifierBuiltinTest, UseCertVerifierBuiltin) {
  net::EmbeddedTestServer test_server(net::EmbeddedTestServer::TYPE_HTTPS);
  net::test_server::RegisterDefaultHandlers(&test_server);
  ASSERT_TRUE(test_server.Start());

  // This just happens to be the only histogram that directly records which
  // verifier was used.
  const char kBuiltinVerifierHistogram[] =
      "Net.CertVerifier.NameNormalizationPrivateRoots.Builtin";

  for (bool builtin_verifier_enabled : {false, true}) {
    SCOPED_TRACE(builtin_verifier_enabled);

    network::mojom::NetworkContextParamsPtr params = CreateContextParams();
    auto creation_params = network::mojom::CertVerifierCreationParams::New();
    creation_params->use_builtin_cert_verifier =
        builtin_verifier_enabled ? network::mojom::CertVerifierCreationParams::
                                       CertVerifierImpl::kBuiltin
                                 : network::mojom::CertVerifierCreationParams::
                                       CertVerifierImpl::kSystem;
    params->cert_verifier_params =
        GetCertVerifierParams(std::move(creation_params));
    std::unique_ptr<network::NetworkContext> network_context =
        CreateContextWithParams(std::move(params));

    network::ResourceRequest request;
    request.url = test_server.GetURL("/nocontent");
    base::HistogramTester histogram_tester;
    std::unique_ptr<network::TestURLLoaderClient> client =
        FetchRequest(request, network_context.get());
    EXPECT_EQ(net::OK, client->completion_status().error_code);
    histogram_tester.ExpectTotalCount(kBuiltinVerifierHistogram,
                                      builtin_verifier_enabled ? 1 : 0);
  }
}

INSTANTIATE_TEST_SUITE_P(All, UseCertVerifierBuiltinTest, ::testing::Bool());

class NetworkContextCertVerifierBuiltinFeatureFlagTest
    : public NetworkContextTest,
      public testing::WithParamInterface<std::tuple<bool, bool>> {
 public:
  NetworkContextCertVerifierBuiltinFeatureFlagTest()
      : use_builtin_cert_verifier_feature_(std::get<0>(GetParam())),
        use_cert_verifier_service_feature_(std::get<1>(GetParam())) {
    std::vector<base::Feature> enabled_features, disabled_features;
    if (use_builtin_cert_verifier_feature_) {
      enabled_features.push_back(net::features::kCertVerifierBuiltinFeature);
    } else {
      disabled_features.push_back(net::features::kCertVerifierBuiltinFeature);
    }
    if (use_cert_verifier_service_feature_) {
#if defined(OS_CHROMEOS)
      // TODO(crbug.com/1085379): remove this GTEST_SKIP().
      GTEST_SKIP() << "Skipping test, CertVerifierService feature not yet "
                      "available on ChromeOS.";
#else
      enabled_features.push_back(network::features::kCertVerifierService);
#endif
    } else {
      disabled_features.push_back(network::features::kCertVerifierService);
    }
    scoped_feature_list_.InitWithFeatures(enabled_features, disabled_features);
  }

  bool use_builtin_cert_verifier_feature() const {
    return use_builtin_cert_verifier_feature_;
  }

 private:
  const bool use_builtin_cert_verifier_feature_;
  const bool use_cert_verifier_service_feature_;
  base::test::ScopedFeatureList scoped_feature_list_;
};

TEST_P(NetworkContextCertVerifierBuiltinFeatureFlagTest,
       DefaultNetworkContextParamsUsesCorrectVerifier) {
  net::EmbeddedTestServer test_server(net::EmbeddedTestServer::TYPE_HTTPS);
  net::test_server::RegisterDefaultHandlers(&test_server);
  ASSERT_TRUE(test_server.Start());

  // This just happens to be the only histogram that directly records which
  // verifier was used.
  const char kBuiltinVerifierHistogram[] =
      "Net.CertVerifier.NameNormalizationPrivateRoots.Builtin";

  // Test creating a NetworkContextParams without specifying a value for
  // use_builtin_cert_verifier. Should use whatever the default cert verifier
  // implementation is according to the feature flag.
  network::mojom::NetworkContextParamsPtr params = CreateContextParams();
  params->cert_verifier_params = GetCertVerifierParams();
  std::unique_ptr<network::NetworkContext> network_context =
      CreateContextWithParams(std::move(params));

  network::ResourceRequest request;
  request.url = test_server.GetURL("/nocontent");
  base::HistogramTester histogram_tester;
  std::unique_ptr<network::TestURLLoaderClient> client =
      FetchRequest(request, network_context.get());
  EXPECT_EQ(net::OK, client->completion_status().error_code);
  histogram_tester.ExpectTotalCount(
      kBuiltinVerifierHistogram, use_builtin_cert_verifier_feature() ? 1 : 0);
}

INSTANTIATE_TEST_SUITE_P(All,
                         NetworkContextCertVerifierBuiltinFeatureFlagTest,
                         ::testing::Combine(::testing::Bool(),
                                            ::testing::Bool()));
#endif  // BUILDFLAG(BUILTIN_CERT_VERIFIER_FEATURE_SUPPORTED)
}  // namespace cert_verifier
