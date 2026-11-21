package main

import (
	_ "embed"
	"encoding/json"
)

//go:embed installer-profiles.json
var profileData []byte

var profiles, captureOnly = readProfiles(profileData)

func readProfiles(data []byte) (map[string]frameworkPair, map[string]bool) {
	var rows map[string]struct {
		App, Core   string
		CaptureOnly bool `json:"capture_only"`
	}
	if err := json.Unmarshal(data, &rows); err != nil {
		panic(err)
	}
	pairs := make(map[string]frameworkPair, len(rows))
	pending := make(map[string]bool)
	for version, row := range rows {
		pairs[version] = frameworkPair{row.App, row.Core}
		if row.CaptureOnly {
			pending[version] = true
		}
	}
	return pairs, pending
}
