const citizenWallet = require("@citizenwallet/sdk");
const ethers = require("ethers");
const fs = require("fs").promises; // For asynchronous file operations
const path = require("path"); // For path manipulation
/**
 * This script is used to configure the card manager for the community.
 * It will create or update the card manager for the community.
 * It will also add the primary token and profile to the card manager.
 * It will also add the primary token and profile to the card manager.
 */
const main = async () => {
  const COMMUNITY_FILE = path.join(__dirname, "../community.json");
  const data = await fs.readFile(COMMUNITY_FILE, "utf8");
  const communityJSON = JSON.parse(data);
  const community = new citizenWallet.CommunityConfig(communityJSON);

  console.log(
    "⚙️ configuring card manager for community: ",
    community.community.name
  );

  const privateKey = process.env.CARD_MANAGER_PRIVATE_KEY;
  if (!privateKey) {
    throw new Error("Private key is not set");
  }

  const signer = new ethers.Wallet(privateKey);

  const cardConfig = community.primarySafeCardConfig;

  console.log(
    "⚙️ configuring card manager private key with instance id:",
    community.primaryCardConfig.instance_id
  );

  const cardManagerMap = {};

  const instance = `${cardConfig.chain_id}:${cardConfig.address}:${cardConfig.instance_id}`;

  if (!cardManagerMap[instance]) {
    const contracts = [];

    const tokens = Object.values(community.tokens).map(
      (token) => token.address
    );

    contracts.push(...tokens);
    contracts.push(community.community.profile.address);

    cardManagerMap[instance] = {
      community,
      contracts,
    };
  }

  const tokens = Object.values(community.tokens).map((token) => token.address);

  cardManagerMap[instance].contracts.push(...tokens);
  cardManagerMap[instance].contracts.push(community.community.profile.address);

  for (const communityMap of Object.values(cardManagerMap)) {
    const signerAccountAddress = await citizenWallet.getAccountAddress(
      communityMap.community,
      signer.address
    );
    if (!signerAccountAddress) {
      throw new Error("Could not find an account for you!");
    }

    const bundler = new citizenWallet.BundlerService(communityMap.community);
    const cardConfig = communityMap.community.primarySafeCardConfig;

    console.log("⚙️ registering instance: ", cardConfig.instance_id);
    console.log(
      "⚙️ configuring for card manager account: ",
      signerAccountAddress
    );
    console.log("⚙️ account based on provided CARD_MANAGER_PRIVATE_KEY");

    console.log("⚙️ whitelisting contracts", communityMap.contracts);

    const owner = await citizenWallet.instanceOwner(communityMap.community);
    if (owner === ethers.ZeroAddress) {
      console.log("⚙️ this is a new instance, creating and whitelisting...");
      const ccalldata = citizenWallet.createInstanceCallData(
        communityMap.community,
        communityMap.contracts
      );

      const hash = await bundler.call(
        signer,
        cardConfig.address,
        signerAccountAddress,
        ccalldata
      );

      console.log("🚀 request submitted:", hash);

      await bundler.awaitSuccess(hash);

      console.log("✅ Instance created");

      continue;
    }
    console.log("⚙️ this instance exists, updating whitelist...");

    const calldata = citizenWallet.updateInstanceContractsCallData(
      communityMap.community,
      communityMap.contracts
    );

    const hash = await bundler.call(
      signer,
      cardConfig.address,
      signerAccountAddress,
      calldata
    );

    console.log("🚀 request submitted:", hash);

    await bundler.awaitSuccess(hash);

    console.log("✅ Instance updated");
  }

  console.log("🎉 configuration completed");
};

main();
