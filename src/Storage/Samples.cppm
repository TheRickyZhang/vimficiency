export module vf.Storage.Samples;

import vf.Versions;

export {

// Completes a request's `vf.journal.SampleParams` into the ones its samples
// are stored under: `vimfy_journal` writing them and the plugin reading them
// must agree. A template because generated headers stay out of module interfaces.
template <typename SampleParams>
void stampSampleParams(SampleParams& params) {
  params.mutable_stamp()->set_scoring_version(SCORING_VERSION);
  params.set_samples_format(SAMPLES_FORMAT);
}

}  // export
