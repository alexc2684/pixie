package controller

import (
	"context"
	"net/http"

	"github.com/graph-gophers/graphql-go"
	"github.com/graph-gophers/graphql-go/relay"
	"pixielabs.ai/pixielabs/src/cloud/api/apienv"
	"pixielabs.ai/pixielabs/src/cloud/api/controller/schema"
	"pixielabs.ai/pixielabs/src/cloud/cloudapipb"
	"pixielabs.ai/pixielabs/src/shared/services/authcontext"
)

// GraphQLEnv holds the GRPC API servers so the GraphQL server can call out to them.
type GraphQLEnv struct {
	VizierClusterServer cloudapipb.VizierClusterServiceServer
}

// QueryResolver resolves queries for GQL.
type QueryResolver struct {
	Env    apienv.APIEnv
	GQLEnv GraphQLEnv
}

// MutationResolver resolves mutations for GQL.
type MutationResolver struct {
	Env apienv.APIEnv
}

// User resolves user information.
func (*QueryResolver) User(ctx context.Context) (*UserInfoResolver, error) {
	sCtx, err := authcontext.FromContext(ctx)
	if err != nil {
		return nil, err
	}
	return &UserInfoResolver{sCtx}, nil
}

// NewGraphQLHandler is the hTTP handler used for handling GraphQL requests.
// TODO(nserrino): Remove apienv when graphqlEnv fully subsumes it.
func NewGraphQLHandler(env apienv.APIEnv, graphqlEnv GraphQLEnv) http.Handler {
	schemaData := schema.MustLoadSchema()
	opts := []graphql.SchemaOpt{graphql.UseFieldResolvers(), graphql.MaxParallelism(20)}
	gqlSchema := graphql.MustParseSchema(schemaData, &QueryResolver{env, graphqlEnv}, opts...)
	return &relay.Handler{Schema: gqlSchema}
}
